#!/usr/bin/env python3
"""Run one Geometry Dash gameplay session and collect DashCore diagnostics only."""
from __future__ import annotations
import argparse
import json
import pathlib
import re
import sys
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import autotest_ab as runtime


def inspect_runtime_log(log_text: str) -> dict[str, Any]:
    """Validate the current DashCore per-level, read-only capture summary."""
    lines = log_text.splitlines()
    marker_lines = [
        line for line in lines if "DashCore_CAPTURE_DIAGNOSTICS" in line
    ]
    marker = marker_lines[-1] if marker_lines else ""

    def token(name: str) -> str | None:
        match = re.search(rf"\b{re.escape(name)}=([^\s,]+)", marker)
        return match.group(1) if match else None

    def integer(name: str) -> int | None:
        value = token(name)
        try:
            return int(value) if value is not None else None
        except ValueError:
            return None

    def decimal(name: str) -> float | None:
        value = token(name)
        try:
            return float(value) if value is not None else None
        except ValueError:
            return None

    values = {
        "frames": integer("frames"),
        "successful_frames": integer("successful"),
        "failed_frames": integer("failed"),
        "capture_enabled": token("capture_enabled"),
        "capture_performed_frames": integer("capture_performed_frames"),
        "capture_skipped_frames": integer("capture_skipped_frames"),
        "index_build_total_ms": decimal("index_build_total_ms"),
        "post_update_avg_ms": decimal("post_update_avg_ms"),
        "post_update_p95_last512_ms": decimal("post_update_p95_last512_ms"),
        "capture_avg_ms": decimal("capture_avg_ms"),
        "capture_p95_last512_ms": decimal("capture_p95_last512_ms"),
        "capture_max_ms": decimal("capture_max_ms"),
        "collection_avg_ms": decimal("collection_avg_ms"),
        "object_array_index_builds_total": integer("object_array_index_builds_total"),
        "object_array_entries_indexed_total": integer("object_array_entries_indexed_total"),
        "objects_cached_in_playlayer_tree_total": integer("objects_cached_in_playlayer_tree_total"),
        "objects_excluded_from_playlayer_tree_index_total": integer(
            "objects_excluded_from_playlayer_tree_index_total"
        ),
        "sprite_candidates_total": integer("sprite_candidates_total"),
        "sprites_accepted_total": integer("sprites_accepted_total"),
        "sprites_skipped_unsupported_flags_total": integer(
            "sprites_skipped_unsupported_flags_total"
        ),
        "sprites_skipped_batch_node_total": integer("sprites_skipped_batch_node_total"),
        "source_failures_total": integer("source_failures_total"),
    }

    checks = {
        "per_level_capture_summary_found": bool(marker),
        "capture_setting_confirmed_on": values["capture_enabled"] == "true",
        "gameplay_frames_observed": bool(values["frames"] and values["frames"] > 0),
        "read_only_capture_attempted": bool(
            values["capture_performed_frames"]
            and values["capture_performed_frames"] > 0
        ),
        "at_least_one_capture_succeeded": bool(
            values["successful_frames"] and values["successful_frames"] > 0
        ),
        "object_array_index_built": bool(
            values["object_array_index_builds_total"]
            and values["object_array_index_builds_total"] > 0
        ),
        "level_objects_indexed": bool(
            values["object_array_entries_indexed_total"]
            and values["object_array_entries_indexed_total"] > 0
        ),
    }
    return {
        "required_checks": checks,
        "all_diagnostics_present": all(checks.values()),
        "capture_summary": values,
        "capture_summary_line": marker,
        "diagnostic_lines": marker_lines,
    }

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--mod-file", type=pathlib.Path, required=True)
    parser.add_argument("--base-dir", default="/sdcard/Android/media/com.geode.launcher")
    parser.add_argument("--report-dir", type=pathlib.Path, required=True)
    parser.add_argument("--device-profile", type=pathlib.Path,
                        default=pathlib.Path("profiles/huawei-y9s.json"))
    parser.add_argument("--level-name", default="DashCore AutoTest")
    parser.add_argument("--level-manifest", type=pathlib.Path,
                        default=pathlib.Path("reports/autotest-level/autotest-level.json"))
    parser.add_argument("--observation-seconds", type=int, default=20)
    args = parser.parse_args()
    if not args.mod_file.is_file():
        raise SystemExit(f"DashCore package not found: {args.mod_file}")
    args.report_dir.mkdir(parents=True, exist_ok=True)
    if not args.level_manifest.is_file():
        raise SystemExit(f"Generated AutoTest level manifest not found: {args.level_manifest}")
    level_manifest = json.loads(args.level_manifest.read_text(encoding="utf-8"))
    if level_manifest.get("result") != "passed" or level_manifest.get("level_name") != args.level_name:
        raise SystemExit("Generated AutoTest level manifest is invalid or names a different level")
    if not 5 <= args.observation_seconds <= 120:
        raise SystemExit("--observation-seconds must be between 5 and 120")

    profile = runtime.load_device_profile(args.device_profile)
    actual_guest = runtime.collect_device_fingerprint(args.serial)
    assessment = runtime.assess_device_profile(profile, actual_guest)
    variant_args = argparse.Namespace(
        serial=args.serial, base_dir=args.base_dir, mod_id="hesta.optgod",
        mod_file=str(args.mod_file), report_dir=str(args.report_dir),
        device_profile_data=profile, device_profile_assessment=assessment,
    )
    print("AUTOTEST-MODE: generated local level; gameplay diagnostics only.")
    print(f"AUTOTEST-LEVEL: {args.level_name} | objects={level_manifest.get('total_objects')}")
    print(f"AUTOTEST-OBSERVATION: {args.observation_seconds}s")
    print("AUTOTEST-CONFIG: capture-object-diagnostics=ON; vanilla rendering remains unchanged.")
    run = runtime.run_variant(
        variant_args, "GAMEPLAY_DIAGNOSTICS", mod_enabled=True,
        culling_mode="off", diagnostics_mode="off", geometry_diagnostics=False,
        render_pipeline_mode="off", render_pipeline_wait_s=0,
        test_level_name=args.level_name,
        observation_seconds=args.observation_seconds,
        capture_object_diagnostics=True,
    )

    session_dir = args.report_dir / "gameplay-diagnostics"
    log_path = session_dir / "combined-geode-log.txt"
    combined_log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    diagnostics = inspect_runtime_log(combined_log)
    gameplay_ok = run.get("gameplay_verified") is True
    level_selection_ok = run.get("level_selection_verified") is True
    crash_free = run.get("crash_detected") is False
    passed = (
        level_selection_ok and gameplay_ok and crash_free
        and run.get("capture_summary_marker_source") is not None
        and diagnostics["all_diagnostics_present"]
    )
    report = {
        "result": "passed" if passed else "incomplete",
        "test_mode": "single_generated_level_gameplay_diagnostics_only",
        "gameplay_verified": gameplay_ok,
        "level_selection_verified": level_selection_ok,
        "test_level": {
            "name": args.level_name,
            "object_count": level_manifest.get("total_objects"),
            "floor_objects": level_manifest.get("floor_objects"),
            "density_sections": level_manifest.get("sections"),
            "manifest": str(args.level_manifest),
            "manifest_sha256": level_manifest.get("output_sha256"),
        },
        "launcher_crash_detected": run.get("crash_detected"),
        "capture_summary_marker_source": run.get("capture_summary_marker_source"),
        "dashcore_package": str(args.mod_file),
        "dashcore_mod_enabled_for_session": True,
        "capture_object_diagnostics": "ON",
        "vanilla_rendering": "UNCHANGED",
        "draw_replacement": False,
        "visibility_or_gameplay_changes": False,
        "automated_gameplay": {
            "level_selection": f"generated local level {args.level_name}; no official or saved-level fallback",
            "level_selection_verified": level_selection_ok,
            "input": f"fixed screen tap at (700,800) every 0.5 seconds during the {args.observation_seconds}-second observation window",
            "observation_seconds": args.observation_seconds,
            "completion_required": False,
            "scope": "uses three reproducible object-density zones and verifies the ATTEMPT HUD before the per-level capture summary is collected",
            "fixture": {
                "object_count": level_manifest.get("total_objects"),
                "floor_objects": level_manifest.get("floor_objects"),
                "sections": level_manifest.get("sections"),
            },
        },
        "device_profile_assessment": assessment,
        "cloud_guest": actual_guest,
        "runtime_metrics": run,
        "diagnostics": diagnostics,
        "limitations": [
            "Cloud Android emulator only; Huawei Y9s / Kirin 710F / Mali-G51 hardware is NOT EMULATED.",
            "This is one capture-enabled DashCore session, not a no-mod baseline or an optimization A/B comparison.",
            "The generated runway contains no gameplay hazards; the fixed tap pattern is a repeatable input stimulus, not a level-completion or correctness test.",
            "The workflow writes the generated local-level save and Geode settings only inside the disposable cloud emulator.",
            "Capture and collection timings are CPU wall-clock measurements, not GPU execution or full-frame presentation timings.",
            "A successful cloud AutoTest verifies instrumentation and repeatability; it does not establish a physical-device FPS improvement.",
        ],
    }
    (args.report_dir / "autotest-gameplay-diagnostics.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    report_lines = [
        "# DashCore AutoTest — generated level and read-only object capture", "",
        f"**Result: {'PASSED' if passed else 'INCOMPLETE'}**", "",
        f"- Dedicated level selected: **{args.level_name} ({level_selection_ok})**",
        f"- In-level HUD verified: **{gameplay_ok}**",
        f"- Fixture object count: **{level_manifest.get('total_objects')}**",
        f"- Observation window: **{args.observation_seconds}s**",
        f"- Launcher crash detected: **{run.get('crash_detected')}**",
        "- DashCore enabled: **yes**",
        "- Capture object diagnostics: **ON**",
        "- Vanilla renderer / gameplay / visibility: **unchanged**",
        "- Passes: **one capture-enabled gameplay run only**",
        "- No-mod baseline / optimization comparison: **not run**",
        f"- DashCore per-level summary source: **{run.get('capture_summary_marker_source')}**",
        f"- Captured gameplay frames: **{diagnostics['capture_summary'].get('frames')}**",
        f"- Successful capture frames: **{diagnostics['capture_summary'].get('successful_frames')}**",
        f"- Indexed game-object entries: **{diagnostics['capture_summary'].get('object_array_entries_indexed_total')}**",
        f"- Capture average / p95 (ms): **{diagnostics['capture_summary'].get('capture_avg_ms')} / {diagnostics['capture_summary'].get('capture_p95_last512_ms')}**",
        f"- Post-update average / p95 (ms): **{diagnostics['capture_summary'].get('post_update_avg_ms')} / {diagnostics['capture_summary'].get('post_update_p95_last512_ms')}**",
        "", "## Diagnostic checks", "",
    ]
    report_lines.extend(
        f"- {name}: **{'PASS' if ok else 'MISSING'}**"
        for name, ok in diagnostics["required_checks"].items()
    )
    report_lines += ["", "## Per-level capture summary", ""]
    report_lines.append(f"- {diagnostics['capture_summary_line'] or 'DashCore_CAPTURE_DIAGNOSTICS marker missing'}")
    report_lines += ["", "## Scope and limitations", "",
                     *[f"- {item}" for item in report["limitations"]]]
    (args.report_dir / "autotest-gameplay-diagnostics.md").write_text(
        "\n".join(report_lines) + "\n", encoding="utf-8"
    )
    print("\n".join(report_lines))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
