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
        "RENDER-UPDATE-BINS-NOTE:", "OBJECT-VISUAL-HIERARCHY:",
        "OBJECT-SECTION-LAYOUT:", "OBJECT-SECTION-ROWS:", "setupHasCompleted",
    )
    selected = [line for line in lines if any(m in line for m in selected_markers)]
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
    required_callbacks = {
        "CCDirector::drawScene", "PlayLayer::postUpdate", "CCNode::visit",
        "CCSprite::draw", "CCSpriteBatchNode::draw", "CCTextureAtlas::drawQuads",
    }
    checks = {
        "render_pipeline_hooks_ready": hooks_ready,
        "full_180_frame_sample": bool(pipeline_match and int(pipeline_match.group(1)) == 180),
        "post_update_summary": "RENDER-UPDATE:" in log_text,
        "update_subphase_summary": "RENDER-UPDATE-SUBPHASES:" in log_text,
        "histogram_summary": "RENDER-UPDATE-BINS-NOTE:" in log_text,
        "render_pipeline_progress_observed": bool(draw_progress),
        "render_update_progress_observed": bool(update_progress),
        "base_layer_update_histogram": "callback=GJBaseGameLayer::update (PlayLayer)" in log_text,
        "visibility_update_histogram": "callback=PlayLayer::updateVisibility" in log_text,
        "required_timing_histograms": required_callbacks.issubset(histogram_callbacks),
        "visual_hierarchy_census": "OBJECT-VISUAL-HIERARCHY:" in log_text,
        "section_layout_census": "OBJECT-SECTION-LAYOUT:" in log_text,
        "static_culling_confirmed_off": culling_off,
    }
    return {
        "required_checks": checks,
        "all_diagnostics_present": all(checks.values()),
        "render_pipeline_frames": int(pipeline_match.group(1)) if pipeline_match else None,
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
        f"- Highest drawScene progress marker: **{diagnostics['last_render_pipeline_progress_frames']}**",
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
