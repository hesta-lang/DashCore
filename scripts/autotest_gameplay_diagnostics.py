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
    lines = log_text.splitlines()
    selected_markers = (
        "RENDER-PIPELINE-HOOKS", "RENDER-PIPELINE FINAL:",
        "RENDER-PIPELINE-PROGRESS:", "RENDER-UPDATE-PROGRESS:", "RENDER-UPDATE:",
        "RENDER-UPDATE-SUBPHASES:", "RENDER-TIMING-BINS:",
        "RENDER-CALLBACK", "RENDER-UPDATE-BINS-NOTE:", "OBJECT-VISUAL-HIERARCHY:",
        "OBJECT-SECTION-LAYOUT:", "OBJECT-SECTION-ROWS:", "setupHasCompleted",
    )
    selected = [line for line in lines if any(m in line for m in selected_markers)]
    final_line = next((line for line in lines if "RENDER-PIPELINE FINAL:" in line), "")
    pipeline_match = re.search(r"RENDER-PIPELINE FINAL: frames=(\d+)", log_text)
    draw_progress = [
        int(value) for value in re.findall(
            r"RENDER-PIPELINE-PROGRESS: drawSceneFrames=(\d+)", log_text
        )
    ]
    update_progress = [
        int(value) for value in re.findall(
            r"RENDER-UPDATE-PROGRESS: postUpdateFrames=(\d+)", log_text
        )
    ]
    final_draw_match = re.search(r"drawSceneFrames=(\d+)", final_line)
    final_update_match = re.search(r"postUpdateFrames=(\d+)", final_line)
    coverage_match = re.search(r"sampleCoverage=([a-z-]+)", final_line)
    final_frames = int(pipeline_match.group(1)) if pipeline_match else None
    draw_scene_frames = (
        int(final_draw_match.group(1)) if final_draw_match
        else (max(draw_progress) if draw_progress else 0)
    )
    post_update_frames = (
        int(final_update_match.group(1)) if final_update_match
        else (final_frames if final_frames is not None
              else (max(update_progress) if update_progress else 0))
    )
    sample_coverage = (
        coverage_match.group(1) if coverage_match
        else ("legacy" if pipeline_match else None)
    )
    hooks_ready = bool(re.search(
        r"RENDER-PIPELINE-HOOKS:.*?ready=true", log_text, re.IGNORECASE
    ))
    culling_off = bool(re.search(
        r"static-culling-requested=false,\s*static-culling-active=false",
        log_text, re.IGNORECASE
    ))
    histogram_lines = [line for line in lines if "RENDER-TIMING-BINS:" in line]
    histogram_callbacks = {
        match.group(1)
        for line in histogram_lines
        if (match := re.search(r"RENDER-TIMING-BINS: callback=([^,]+)", line))
    }
    callback_metrics: dict[str, dict[str, int | float]] = {}
    for line in lines:
        match = re.search(
            r"RENDER-CALLBACK (.+?): calls=(\d+), timedCalls=(\d+), "
            r"inclusiveAvgUs=([0-9.]+), inclusiveTotalUs=(\d+), inclusiveMaxUs=(\d+)",
            line,
        )
        if match:
            name, calls, timed_calls, average_us, total_us, max_us = match.groups()
            callback_metrics[name] = {
                "calls": int(calls),
                "timed_calls": int(timed_calls),
                "inclusive_avg_us": float(average_us),
                "inclusive_total_us": int(total_us),
                "inclusive_max_us": int(max_us),
            }
    draw_callback_names = (
        "CCSprite::draw",
        "CCSpriteBatchNode::draw",
        "CCTextureAtlas::drawQuads",
        "CCTextureAtlas::drawNumberOfQuads",
    )
    draw_callbacks_observed = [
        name for name in draw_callback_names
        if callback_metrics.get(name, {}).get("timed_calls", 0) > 0
    ]
    node_visit_observed = (
        callback_metrics.get("CCNode::visit", {}).get("timed_calls", 0) > 0
    )
    # CCDirector::drawScene remains supplementary. Renderer callback timings
    # use independent per-callback sampling and do not depend on it.
    required_callbacks = {
        "PlayLayer::postUpdate", "CCNode::visit",
        "CCSprite::draw", "CCSpriteBatchNode::draw", "CCTextureAtlas::drawQuads",
    }
    checks = {
        "render_pipeline_hooks_ready": hooks_ready,
        "full_180_frame_sample": bool(
            pipeline_match and final_frames == 180 and post_update_frames == 180
        ),
        "post_update_summary": "RENDER-UPDATE:" in log_text,
        "update_subphase_summary": "RENDER-UPDATE-SUBPHASES:" in log_text,
        "histogram_summary": "RENDER-UPDATE-BINS-NOTE:" in log_text,
        "render_pipeline_progress_observed": bool(draw_progress or update_progress),
        "render_update_progress_observed": bool(update_progress),
        "base_layer_update_histogram": "callback=GJBaseGameLayer::update (PlayLayer)" in log_text,
        "visibility_update_histogram": "callback=PlayLayer::updateVisibility" in log_text,
        "required_timing_histograms": required_callbacks.issubset(histogram_callbacks),
        "render_callback_summary_present": bool(callback_metrics),
        "renderer_draw_callback_timing_observed": bool(draw_callbacks_observed),
        "visual_hierarchy_census": "OBJECT-VISUAL-HIERARCHY:" in log_text,
        "section_layout_census": "OBJECT-SECTION-LAYOUT:" in log_text,
        "static_culling_confirmed_off": culling_off,
    }
    return {
        "required_checks": checks,
        "all_diagnostics_present": all(checks.values()),
        "render_pipeline_frames": final_frames,
        "post_update_frames": post_update_frames,
        "draw_scene_frames": draw_scene_frames,
        "sample_coverage": sample_coverage,
        "draw_scene_callback_observed": draw_scene_frames > 0,
        "render_callback_metrics": callback_metrics,
        "node_visit_callback_observed": node_visit_observed,
        "renderer_draw_callback_names_observed": draw_callbacks_observed,
        "renderer_draw_callback_timing_observed": bool(draw_callbacks_observed),
        "last_render_pipeline_progress_frames": max(draw_progress) if draw_progress else None,
        "last_render_update_progress_frames": max(update_progress) if update_progress else None,
        "timing_histogram_lines": histogram_lines,
        "histogram_callbacks_found": sorted(histogram_callbacks),
        "diagnostic_lines": selected,
    }

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--mod-file", type=pathlib.Path, required=True)
    parser.add_argument("--base-dir", default="/sdcard/Android/media/com.geode.launcher")
    parser.add_argument("--report-dir", type=pathlib.Path, required=True)
    parser.add_argument("--device-profile", type=pathlib.Path,
                        default=pathlib.Path("profiles/huawei-y9s.json"))
    args = parser.parse_args()
    if not args.mod_file.is_file():
        raise SystemExit(f"DashCore package not found: {args.mod_file}")
    args.report_dir.mkdir(parents=True, exist_ok=True)

    profile = runtime.load_device_profile(args.device_profile)
    actual_guest = runtime.collect_device_fingerprint(args.serial)
    assessment = runtime.assess_device_profile(profile, actual_guest)
    variant_args = argparse.Namespace(
        serial=args.serial, base_dir=args.base_dir, mod_id="hesta.optgod",
        mod_file=str(args.mod_file), report_dir=str(args.report_dir),
        device_profile_data=profile, device_profile_assessment=assessment,
    )
    print("AUTOTEST-MODE: one gameplay session; diagnostics only.")
    print("AUTOTEST-CONFIG: Performance Diagnostics=OFF; Render Pipeline Diagnostics=ON; static culling=OFF.")
    run = runtime.run_variant(
        variant_args, "GAMEPLAY_DIAGNOSTICS", mod_enabled=True,
        culling_mode="off", diagnostics_mode="off", geometry_diagnostics=False,
        render_pipeline_mode="on",
        render_pipeline_wait_s=60,
    )

    session_dir = args.report_dir / "gameplay-diagnostics"
    log_path = session_dir / "combined-geode-log.txt"
    combined_log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    diagnostics = inspect_runtime_log(combined_log)
    gameplay_ok = run.get("gameplay_verified") is True
    crash_free = run.get("crash_detected") is False
    passed = gameplay_ok and crash_free and diagnostics["all_diagnostics_present"]
    report = {
        "result": "passed" if passed else "incomplete",
        "test_mode": "single_gameplay_diagnostics_only",
        "gameplay_verified": gameplay_ok,
        "launcher_crash_detected": run.get("crash_detected"),
        "render_pipeline_sample_marker_source": run.get("render_pipeline_sample_marker_source"),
        "dashcore_package": str(args.mod_file),
        "dashcore_mod_enabled_for_session": True,
        "performance_diagnostics": "OFF",
        "render_pipeline_diagnostics": "ON",
        "static_culling": "OFF",
        "automated_gameplay": {
            "level_selection": "last saved supported level when available; otherwise first standard level",
            "input": "fixed screen tap at (700,800) every 0.5 seconds during the 20-second observation window",
            "completion_required": False,
            "scope": "verifies the in-level ATTEMPT HUD and keeps gameplay active while the 180-frame diagnostic sample is collected",
        },
        "device_profile_assessment": assessment,
        "cloud_guest": actual_guest,
        "runtime_metrics": run,
        "diagnostics": diagnostics,
        "limitations": [
            "Cloud Android emulator only; Huawei Y9s / Kirin 710F / Mali-G51 hardware is NOT EMULATED.",
            "This AutoTest performs one DashCore-enabled gameplay session only; there is no no-mod baseline or culling-ON pass.",
            "The fixed tap pattern is a gameplay stimulus, not a level-completion or gameplay-correctness test.",
            "CPU callback timings are inclusive and include instrumentation overhead; they are not GPU execution timings.",
            "When CCDirector::drawScene is unavailable, its supplementary timing is omitted; Cocos callback coverage is reported independently. CPU callback timings are not GPU execution timings.",
            "A successful cloud AutoTest does not establish a physical-device FPS improvement.",
        ],
    }
    (args.report_dir / "autotest-gameplay-diagnostics.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    report_lines = [
        "# DashCore AutoTest — gameplay and diagnostics only", "",
        f"**Result: {'PASSED' if passed else 'INCOMPLETE'}**", "",
        f"- In-level HUD verified: **{gameplay_ok}**",
        f"- Launcher crash detected: **{run.get('crash_detected')}**",
        "- DashCore enabled: **yes**",
        "- Performance Diagnostics: **OFF**",
        "- Render Pipeline Diagnostics: **ON**",
        "- Static culling: **OFF**",
        "- Passes: **one gameplay run only**",
        "- Baseline / culling-ON comparisons: **not run**",
        f"- Full 180-frame render sample: **{diagnostics['render_pipeline_frames'] == 180}**",
        f"- Highest Director drawScene progress marker: **{diagnostics['last_render_pipeline_progress_frames']}**",
        f"- Director drawScene callbacks observed: **{diagnostics['draw_scene_callback_observed']}**",
        f"- Director supplementary sample coverage: **{diagnostics['sample_coverage']}**",
        f"- CCNode::visit timed callback observed: **{diagnostics['node_visit_callback_observed']}**",
        f"- Timed sprite / batch / atlas draw callbacks: **{', '.join(diagnostics['renderer_draw_callback_names_observed']) or 'none'}**",
        f"- Highest postUpdate progress marker: **{diagnostics['last_render_update_progress_frames']}**",
        f"- Completion marker source: **{run.get('render_pipeline_sample_marker_source')}**",
        "", "## Diagnostic checks", "",
    ]
    report_lines.extend(
        f"- {name}: **{'PASS' if ok else 'MISSING'}**"
        for name, ok in diagnostics["required_checks"].items()
    )
    report_lines += ["", "## Timing histograms", ""]
    report_lines.extend(f"- {line}" for line in diagnostics["timing_histogram_lines"])
    report_lines += ["", "## Scope and limitations", "",
                     *[f"- {item}" for item in report["limitations"]]]
    (args.report_dir / "autotest-gameplay-diagnostics.md").write_text(
        "\n".join(report_lines) + "\n", encoding="utf-8"
    )
    print("\n".join(report_lines))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
