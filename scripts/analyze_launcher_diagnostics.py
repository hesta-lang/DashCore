#!/usr/bin/env python3
"""Summarize Launcher Diagnostics JSONL sessions and compare capture OFF/ON runs.

Standard-library only. Callback timing is never described as GPU execution or
actual display-presentation timing.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import statistics
import zipfile
from typing import Any

MAX_INPUT_BYTES = 64 * 1024 * 1024
MAX_ZIP_ENTRY_BYTES = 16 * 1024 * 1024


def _number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    return value if math.isfinite(value) else None


def read_session(data: bytes, label: str) -> dict[str, Any]:
    records = []
    errors = []
    for line_no, line in enumerate(data.decode("utf-8-sig", errors="replace").splitlines(), 1):
        if not line.strip():
            continue
        try:
            item = json.loads(line)
        except json.JSONDecodeError as error:
            errors.append(f"line {line_no}: {error.msg}")
            continue
        if not isinstance(item, dict):
            errors.append(f"line {line_no}: JSON value is not an object")
            continue
        records.append(item)

    grouped: dict[str, list[dict[str, Any]]] = {}
    for item in records:
        kind = item.get("type")
        if isinstance(kind, str):
            grouped.setdefault(kind, []).append(item)

    device = grouped.get("device_info", [{}])[0]
    start = grouped.get("session_start", [{}])[0].get("attributes", {})
    end_records = grouped.get("session_end", [])
    end = end_records[-1].get("attributes", {}) if end_records else {}
    samples = [r.get("metrics", {}) for r in grouped.get("process_sample", [])
               if isinstance(r.get("metrics"), dict)]
    windows = [r.get("metrics", {}) for r in grouped.get("render_window", [])
               if isinstance(r.get("metrics"), dict)]
    dashcore = [r.get("attributes", {}) for r in grouped.get("dashcore_level_capture_diagnostics", [])
                if isinstance(r.get("attributes"), dict)]
    starts = [r.get("attributes", {}) for r in grouped.get("level_started", [])]
    ends = [r.get("attributes", {}) for r in grouped.get("level_ended", [])]

    warnings = []
    if errors:
        warnings.append(f"{len(errors)} malformed JSONL line(s) were skipped.")
    if not device:
        warnings.append("Missing device_info record.")
    if not grouped.get("session_start"):
        warnings.append("Missing session_start record.")
    if not end_records:
        warnings.append("Missing session_end; this recording may have been interrupted.")
    if not windows:
        warnings.append("No render_window records were found.")
    if not samples:
        warnings.append("No process_sample records were found.")
    if not starts:
        warnings.append("No level_started markers were found.")
    if len(starts) > len(ends):
        warnings.append("One or more level_started markers have no matching level_ended marker.")

    capture_states = sorted({bool(r["capture_enabled"]) for r in dashcore
                             if isinstance(r.get("capture_enabled"), bool)})
    if not dashcore:
        warnings.append("No DashCore level diagnostics; capture mode is unknown.")
    elif len(capture_states) > 1:
        warnings.append("Capture mode changed during the session; this is not a single-variant run.")
    elif not capture_states:
        warnings.append("DashCore records do not declare capture_enabled.")

    def aggregate(scope_windows: list[dict[str, Any]], scope: str) -> dict[str, Any]:
        rows = []
        for window in scope_windows:
            data = window.get(scope, {})
            if isinstance(data, dict) and int(data.get("frame_count", 0) or 0) > 0:
                rows.append(data)
        result: dict[str, Any] = {
            "window_count": len(rows),
            "frame_count": sum(int(r.get("frame_count", 0) or 0) for r in rows),
            "over_1_5x_refresh_budget": sum(
                int(r.get("intervals_over_1_5x_refresh_budget", 0) or 0) for r in rows
            ),
        }
        for source, target in (
            ("frame_interval_ms", "frame_interval"),
            ("renderer_callback_wall_ms", "callback_wall"),
            ("renderer_callback_thread_cpu_ms", "callback_thread_cpu"),
        ):
            stats = [r.get(source, {}) for r in rows if isinstance(r.get(source), dict)]
            weighted = [(_number(s.get("avg")), int(s.get("sample_count", 0) or 0)) for s in stats]
            weighted = [(v, n) for v, n in weighted if v is not None and n > 0]
            p50 = [_number(s.get("p50")) for s in stats]
            p95 = [_number(s.get("p95")) for s in stats]
            maximum = [_number(s.get("max")) for s in stats]
            count = sum(n for _, n in weighted)
            result[target] = {
                "weighted_average_ms": sum(v * n for v, n in weighted) / count if count else None,
                "worst_window_p50_ms": max((v for v in p50 if v is not None), default=None),
                "worst_window_p95_ms": max((v for v in p95 if v is not None), default=None),
                "maximum_ms": max((v for v in maximum if v is not None), default=None),
                "sample_count": count,
                "p95_method": "maximum p95 among individual windows; not merged session p95",
            }
        result["windows_with_max_interval_over_50ms"] = 0
        result["windows_with_max_interval_over_100ms"] = 0
        result["windows_with_max_interval_over_250ms"] = 0
        for row in rows:
            maximum = _number(row.get("frame_interval_ms", {}).get("max"))
            if maximum is not None:
                for threshold in (50, 100, 250):
                    if maximum > threshold:
                        key = f"windows_with_max_interval_over_{threshold}ms"
                        result[key] += 1
        return result

    def process_series(key: str) -> dict[str, Any]:
        values = [v for sample in samples if (v := _number(sample.get(key))) is not None]
        if not values:
            return {"sample_count": 0, "average": None, "p95": None, "maximum": None}
        ordered = sorted(values)
        return {
            "sample_count": len(values),
            "average": statistics.mean(values),
            "p95": ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)],
            "maximum": max(values),
        }

    levels: dict[str, list[dict[str, Any]]] = {}
    names: dict[str, str] = {}
    for window in windows:
        level_data = window.get("level_renderer", {})
        if not isinstance(level_data, dict) or int(level_data.get("frame_count", 0) or 0) <= 0:
            continue
        level_id, level_name = window.get("level_id"), window.get("level_name")
        identity = str(level_id) if level_id not in (None, "", "null") else str(level_name or "")
        key = identity.strip() or f"segment-{window.get('level_segment_index', 'unknown')}"
        levels.setdefault(key, []).append(window)
        names[key] = str(level_name or level_id or key)

    summaries = []
    for item in dashcore:
        summaries.append({key: item[key] for key in (
            "capture_enabled", "capture_skipped_frames", "frame_count", "index_build_total_ms",
            "object_array_entries_indexed_total", "objects_cached_in_playlayer_tree_total",
            "objects_excluded_from_playlayer_tree_index_total", "root_objects_total",
            "sprites_accepted_total", "sprites_skipped_batch_node_total",
            "sprites_skipped_unsupported_flags_total", "post_update_avg_ms",
            "post_update_p95_ms", "post_update_max_ms", "capture_avg_ms",
            "capture_max_ms", "collection_avg_ms", "collection_max_ms"
        ) if key in item})

    return {
        "source": label,
        "valid_json_records": len(records),
        "record_counts": {k: len(v) for k, v in sorted(grouped.items())},
        "parse_errors": errors,
        "complete": not errors and bool(device) and bool(grouped.get("session_start"))
                    and bool(end_records) and bool(windows),
        "warnings": warnings,
        "device": {key: device.get(key) for key in (
            "manufacturer", "model", "device", "board", "hardware", "android_release",
            "android_sdk", "supported_abis", "launcher_version", "game_version",
            "game_version_code", "screen_width_px", "screen_height_px",
            "initial_refresh_rate_hz", "configured_frame_budget_hz", "gl_vendor",
            "gl_renderer", "gl_version"
        ) if key in device},
        "session": {
            "refresh_rate_hz": start.get("refresh_rate_hz", device.get("initial_refresh_rate_hz")),
            "frame_budget_hz": start.get("frame_budget_hz", device.get("configured_frame_budget_hz")),
            "duration_ms": end.get("duration_ms"),
            "end_reason": end.get("reason"),
            "total_game_frames": end.get("game_frame_count_total"),
            "total_level_frames": end.get("level_frame_count_total"),
            "level_segments_started": len(starts),
            "level_segments_ended": len(ends),
        },
        "renderer": {
            "game": aggregate(windows, "game_renderer"),
            "level": aggregate(windows, "level_renderer"),
        },
        "process": {
            "cpu_percent_of_one_core": process_series("process_cpu_percent_of_one_core"),
            "pss_kb": process_series("process_pss_kb"),
            "available_ram_kb": process_series("system_ram_available_kb"),
            "native_heap_kb": process_series("native_heap_allocated_kb"),
            "thread_count": process_series("process_thread_count"),
            "thermal_statuses": sorted({str(s.get("thermal_status_label")) for s in samples
                                        if s.get("thermal_status_label") is not None}),
        },
        "capture_enabled": capture_states[0] if len(capture_states) == 1 else None,
        "capture_state_mixed": len(capture_states) > 1,
        "dashcore_level_summaries": summaries,
        "levels": [{"key": key, "name": names[key],
                    "renderer": aggregate(rows, "level_renderer"),
                    "window_count": len(rows)}
                   for key, rows in sorted(levels.items())],
        "limitations": [
            "Cocos callback intervals and callback durations are not GPU execution or display-presentation timing.",
            "Window p95 values are not merged into an exact session p95; worst-window p95 is reported.",
            "Threshold counters count windows whose maximum exceeded a threshold, not every individual slow frame.",
        ],
    }


def load_inputs(path: pathlib.Path) -> list[dict[str, Any]]:
    if path.is_dir():
        files = sorted(path.rglob("*.jsonl"))
        if not files:
            raise ValueError(f"No JSONL files found under {path}")
        result = []
        for file in files:
            if file.stat().st_size > MAX_INPUT_BYTES:
                raise ValueError(f"Input is too large: {file}")
            result.append(read_session(file.read_bytes(), str(file)))
        return result
    if not path.is_file():
        raise FileNotFoundError(path)
    if path.stat().st_size > MAX_INPUT_BYTES:
        raise ValueError(f"Input exceeds the 64 MiB limit: {path}")
    if zipfile.is_zipfile(path):
        result, total = [], 0
        with zipfile.ZipFile(path) as archive:
            entries = [e for e in archive.infolist()
                       if not e.is_dir() and e.filename.lower().endswith(".jsonl")]
            if not entries:
                raise ValueError(f"No JSONL sessions found in {path}")
            for entry in entries:
                if entry.file_size > MAX_ZIP_ENTRY_BYTES:
                    raise ValueError(f"Archive entry too large: {entry.filename}")
                total += entry.file_size
                if total > MAX_INPUT_BYTES:
                    raise ValueError("Uncompressed JSONL data exceeds the 64 MiB limit.")
                result.append(read_session(archive.read(entry), f"{path.name}:{entry.filename}"))
        return result
    if path.suffix.lower() != ".jsonl":
        raise ValueError(f"Expected JSONL, ZIP, or a directory: {path}")
    return [read_session(path.read_bytes(), str(path))]


def _level_map(session: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {item["key"]: item for item in session.get("levels", [])}


def _delta(before: Any, after: Any) -> dict[str, Any]:
    if not isinstance(before, (int, float)) or not isinstance(after, (int, float)):
        return {"before": before, "after": after, "delta": None, "delta_percent": None}
    diff = after - before
    return {"before": before, "after": after, "delta": diff,
            "delta_percent": diff / before * 100.0 if before else None}


def compare_sessions(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    checks = {}
    for key in ("manufacturer", "model", "device", "android_release", "android_sdk", "game_version"):
        left, right = baseline["device"].get(key), candidate["device"].get(key)
        checks[f"same_{key}"] = left is not None and right is not None and left == right
    for key in ("refresh_rate_hz", "frame_budget_hz"):
        left, right = baseline["session"].get(key), candidate["session"].get(key)
        checks[f"same_{key}"] = left is not None and right is not None and left == right
    left_levels, right_levels = _level_map(baseline), _level_map(candidate)
    matched = sorted(set(left_levels) & set(right_levels))
    checks["same_level_observed"] = bool(matched)
    checks["baseline_capture_off"] = baseline.get("capture_enabled") is False
    checks["candidate_capture_on"] = candidate.get("capture_enabled") is True
    checks["single_capture_variant_each"] = (
        baseline.get("capture_enabled") is not None and candidate.get("capture_enabled") is not None
        and not baseline.get("capture_state_mixed") and not candidate.get("capture_state_mixed")
    )
    level_results = []
    for key in matched:
        before, after = left_levels[key]["renderer"], right_levels[key]["renderer"]
        before_frame, after_frame = before.get("frame_interval", {}), after.get("frame_interval", {})
        before_wall, after_wall = before.get("callback_wall", {}), after.get("callback_wall", {})
        level_results.append({
            "key": key, "name": left_levels[key]["name"],
            "frame_interval_avg_ms": _delta(before_frame.get("weighted_average_ms"),
                                             after_frame.get("weighted_average_ms")),
            "frame_interval_worst_window_p95_ms": _delta(before_frame.get("worst_window_p95_ms"),
                                                          after_frame.get("worst_window_p95_ms")),
            "callback_wall_avg_ms": _delta(before_wall.get("weighted_average_ms"),
                                           after_wall.get("weighted_average_ms")),
            "callback_thread_cpu_avg_ms": _delta(
                before.get("callback_thread_cpu", {}).get("weighted_average_ms"),
                after.get("callback_thread_cpu", {}).get("weighted_average_ms")),
        })
    ok = all(checks.values())
    return {
        "status": "metadata-compatible-not-route-verified" if ok else "not-a-controlled-pair",
        "metadata_compatible": ok,
        "checks": checks,
        "matched_levels": level_results,
        "manual_confirmation_required": [
            "Confirm both runs used the same level route and the same StartPos; these are not recorded by the current launcher schema.",
            "Keep graphics settings, background apps, refresh rate and device rest conditions similar.",
            "This comparison does not measure GPU execution or prove an optimization.",
        ],
        "process_comparison": {
            "cpu_percent_of_one_core": _delta(
                baseline["process"]["cpu_percent_of_one_core"].get("average"),
                candidate["process"]["cpu_percent_of_one_core"].get("average")),
            "pss_kb": _delta(baseline["process"]["pss_kb"].get("average"),
                             candidate["process"]["pss_kb"].get("average")),
        },
    }


def _fmt(value: Any, suffix: str = "") -> str:
    return "n/d" if value is None else f"{value:.3f}{suffix}" if isinstance(value, float) else f"{value}{suffix}"


def render_markdown(sessions: list[dict[str, Any]], comparison: dict[str, Any] | None) -> str:
    lines = ["# Launcher Diagnostics — automatic report", "",
             f"Sessions analyzed: **{len(sessions)}**", "",
             "> CPU callback timing is not GPU execution or actual display-presentation timing.", ""]
    for index, session in enumerate(sessions, 1):
        game, level = session["renderer"]["game"], session["renderer"]["level"]
        frame = game["frame_interval"]
        lines += [
            f"## Session {index}: {session['source']}", "",
            f"- Completeness: **{'complete' if session['complete'] else 'incomplete'}**",
            f"- Device: **{session['device'].get('manufacturer', 'unknown')} {session['device'].get('model', '')}**",
            f"- Game / launcher: **{session['device'].get('game_version', 'unknown')} / {session['device'].get('launcher_version', 'unknown')}**",
            f"- Duration: **{_fmt(session['session'].get('duration_ms'), ' ms')}**",
            f"- DashCore capture: **{session['capture_enabled'] if session['capture_enabled'] is not None else 'unknown/mixed'}**",
            f"- Renderer frames: **{game['frame_count']}**; level frames: **{level['frame_count']}**",
            f"- Average frame interval: **{_fmt(frame.get('weighted_average_ms'), ' ms')}**",
            f"- Worst one-second-window p95: **{_fmt(frame.get('worst_window_p95_ms'), ' ms')}**",
            f"- Largest recorded interval: **{_fmt(frame.get('maximum_ms'), ' ms')}**",
            f"- Windows with max interval >50 / >100 / >250 ms: **{game['windows_with_max_interval_over_50ms']} / {game['windows_with_max_interval_over_100ms']} / {game['windows_with_max_interval_over_250ms']}**",
            f"- Average process CPU: **{_fmt(session['process']['cpu_percent_of_one_core'].get('average'), '% of one core')}**",
            f"- PSS average / maximum: **{_fmt(session['process']['pss_kb'].get('average'), ' KB')} / {_fmt(session['process']['pss_kb'].get('maximum'), ' KB')}**", "",
        ]
        if session["warnings"]:
            lines += ["### Warnings", ""] + [f"- {warning}" for warning in session["warnings"]] + [""]
        if session["dashcore_level_summaries"]:
            lines += ["### DashCore level summaries", ""]
            lines += [json.dumps(item, sort_keys=True) for item in session["dashcore_level_summaries"]]
            lines.append("")
        if session["levels"]:
            lines += ["### Per-level timing", "",
                      "| Level | Frames | Avg interval | Worst-window p95 | Max interval |",
                      "|---|---:|---:|---:|---:|"]
            for item in session["levels"]:
                metric = item["renderer"]["frame_interval"]
                lines.append(f"| {item['name']} | {item['renderer']['frame_count']} | "
                             f"{_fmt(metric.get('weighted_average_ms'), ' ms')} | "
                             f"{_fmt(metric.get('worst_window_p95_ms'), ' ms')} | "
                             f"{_fmt(metric.get('maximum_ms'), ' ms')} |")
            lines.append("")
    if comparison:
        lines += ["## Capture OFF vs ON comparison", "",
                  f"**Status: {comparison['status']}**", "",
                  "| Check | Result |", "|---|---:|"]
        lines += [f"| {key} | {'PASS' if value else 'NOT CONFIRMED'} |"
                  for key, value in comparison["checks"].items()]
        lines += ["", "| Level | Avg interval change | Worst-window p95 change | Callback wall change |",
                  "|---|---:|---:|---:|"]
        for item in comparison["matched_levels"]:
            lines.append(f"| {item['name']} | "
                         f"{_fmt(item['frame_interval_avg_ms']['delta_percent'], '%')} | "
                         f"{_fmt(item['frame_interval_worst_window_p95_ms']['delta_percent'], '%')} | "
                         f"{_fmt(item['callback_wall_avg_ms']['delta_percent'], '%')} |")
        lines += ["", "### Manual checks", ""] + [
            f"- {value}" for value in comparison["manual_confirmation_required"]]
    return "\n".join(lines).rstrip() + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="JSONL file, ZIP of JSONL sessions, or directory")
    parser.add_argument("--compare", nargs=2, metavar=("BASELINE_OFF", "CAPTURE_ON"),
                        help="Compare two single-session JSONL files or ZIP archives, OFF then ON.")
    parser.add_argument("--output-dir", type=pathlib.Path,
                        default=pathlib.Path("reports/launcher-diagnostics"))
    args = parser.parse_args(argv)
    sessions = []
    for value in args.inputs:
        sessions.extend(load_inputs(pathlib.Path(value)))
    comparison = None
    if args.compare:
        baseline = load_inputs(pathlib.Path(args.compare[0]))
        candidate = load_inputs(pathlib.Path(args.compare[1]))
        if len(baseline) != 1 or len(candidate) != 1:
            parser.error("--compare inputs must each contain exactly one JSONL session.")
        comparison = compare_sessions(baseline[0], candidate[0])
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {"schema_version": 1, "sessions": sessions, "comparison": comparison}
    json_path = args.output_dir / "launcher-diagnostics-summary.json"
    md_path = args.output_dir / "launcher-diagnostics-summary.md"
    json_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    md_path.write_text(render_markdown(sessions, comparison), encoding="utf-8")
    print(f"JSON report: {json_path}")
    print(f"Markdown report: {md_path}")
    print(f"Sessions analyzed: {len(sessions)}")
    if comparison:
        print(f"A/B status: {comparison['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
