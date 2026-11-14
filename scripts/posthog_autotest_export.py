#!/usr/bin/env python3
"""Export a privacy-minimized DashCore AutoTest summary to PostHog."""
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


def build_events(report: dict[str, Any], repo: str, run_id: str,
                 run_number: str, sha: str, ref: str, run_url: str) -> list[dict[str, Any]]:
    diagnostics = report.get("diagnostics") or {}
    inventory = diagnostics.get("callback_inventory") or {}
    checks = diagnostics.get("required_checks") or {}
    distinct_id = f"dashcore-autotest:{repo}:{run_id}"[:200]
    summary = {
        "distinct_id": distinct_id,
        "event": "dashcore_autotest_completed",
        "properties": {
            "repository": repo, "workflow_run_id": str(run_id),
            "workflow_run_number": str(run_number), "commit_sha": sha,
            "git_ref": ref, "workflow_run_url": run_url,
            "result": report.get("result", "unknown"),
            "test_mode": report.get("test_mode", "unknown"),
            "gameplay_verified": bool(report.get("gameplay_verified", False)),
            "launcher_crash_detected": report.get("launcher_crash_detected"),
            "render_pipeline_frames": diagnostics.get("render_pipeline_frames"),
            "post_update_frames": diagnostics.get("post_update_frames"),
            "draw_scene_frames": diagnostics.get("draw_scene_frames"),
            "sample_coverage": diagnostics.get("sample_coverage"),
            "callback_inventory_complete": bool(checks.get("callback_inventory_complete", False)),
            "callbacks_running_count": len(diagnostics.get("callbacks_running") or []),
            "callbacks_absent_count": len(diagnostics.get("callbacks_absent") or []),
            "callbacks_enabled_not_observed_count": len(diagnostics.get("callbacks_enabled_not_observed") or []),
            "callbacks_disabled_count": len(diagnostics.get("callbacks_disabled") or []),
            "required_diagnostics_passed": sum(value is True for value in checks.values()),
            "required_diagnostics_total": len(checks),
            "static_culling": report.get("static_culling"),
            "performance_diagnostics": report.get("performance_diagnostics"),
            "render_pipeline_diagnostics": report.get("render_pipeline_diagnostics"),
        },
    }
    events = [summary]
    for name, callback in sorted(inventory.items()):
        events.append({
            "distinct_id": distinct_id, "event": "dashcore_autotest_callback",
            "properties": {
                "repository": repo, "workflow_run_id": str(run_id),
                "workflow_run_url": run_url, "commit_sha": sha,
                "callback_name": name, "state": callback.get("state", "UNKNOWN"),
                "hook_found": callback.get("hook_found"),
                "hook_enabled": callback.get("hook_enabled"),
                "calls": callback.get("calls", 0),
                "timed_calls": callback.get("timed_calls", 0),
            },
        })
    return events


def send_event(host: str, token: str, event: dict[str, Any]) -> None:
    request = urllib.request.Request(
        host.rstrip("/") + "/i/v0/e/",
        data=json.dumps({"api_key": token, **event}, separators=(",", ":")).encode("utf-8"),
        headers={"Content-Type": "application/json", "User-Agent": "DashCore-AutoTest/1.0"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        if response.status < 200 or response.status >= 300:
            raise RuntimeError(f"PostHog returned HTTP {response.status}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    token = os.environ.get("POSTHOG_PROJECT_TOKEN", "").strip()
    if not token:
        print("POSTHOG-EXPORT: skipped (POSTHOG_PROJECT_TOKEN secret is not configured).")
        return 0
    if not args.report.is_file():
        print(f"POSTHOG-EXPORT: skipped (report not found: {args.report}).")
        return 0
    report = json.loads(args.report.read_text(encoding="utf-8"))
    repo = os.environ.get("GITHUB_REPOSITORY", "unknown")
    run_id = os.environ.get("GITHUB_RUN_ID", "unknown")
    run_url = (os.environ.get("GITHUB_SERVER_URL", "https://github.com").rstrip("/")
               + "/" + repo + "/actions/runs/" + run_id)
    events = build_events(
        report, repo, run_id, os.environ.get("GITHUB_RUN_NUMBER", "unknown"),
        os.environ.get("GITHUB_SHA", "unknown"), os.environ.get("GITHUB_REF", "unknown"),
        run_url,
    )
    host = os.environ.get("POSTHOG_HOST", "https://us.i.posthog.com")
    try:
        for event in events:
            send_event(host, token, event)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, RuntimeError) as exc:
        print(f"::warning::PostHog export failed: {type(exc).__name__}")
        return 1
    print(f"POSTHOG-EXPORT: sent 1 run summary and {len(events) - 1} callback events.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
