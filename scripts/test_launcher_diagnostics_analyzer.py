#!/usr/bin/env python3
"""Contract tests for the Launcher Diagnostics JSONL analyzer."""
from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import analyze_launcher_diagnostics as analyzer


def record(kind, **fields):
    return {"type": kind, **fields}


def sample_session(capture_enabled=False, interval_avg=16.7, p95=22.0, max_ms=45.0):
    common_stats = {
        "frame_interval_ms": {
            "sample_count": 100, "avg": interval_avg, "p50": interval_avg,
            "p95": p95, "max": max_ms
        },
        "renderer_callback_wall_ms": {
            "sample_count": 100, "avg": 3.0, "p50": 2.8, "p95": 4.0, "max": 7.0
        },
        "renderer_callback_thread_cpu_ms": {
            "sample_count": 100, "avg": 1.4, "p50": 1.2, "p95": 2.0, "max": 3.0
        },
        "intervals_over_1_5x_refresh_budget": 4,
        "frame_count": 100,
    }
    return [
        record("device_info", manufacturer="HUAWEI", model="STK-LX3", device="HWSTK",
               android_release="10", android_sdk=29, game_version="2.2081",
               launcher_version="1.9.2-alpha", initial_refresh_rate_hz=60.0),
        record("session_start", attributes={"refresh_rate_hz": 60.0, "frame_budget_hz": 60.0}),
        record("level_started", attributes={"level_segment_index": 1, "level_id": "123", "level_name": "Test Level"}),
        record("process_sample", metrics={"process_cpu_percent_of_one_core": 82.0,
               "process_pss_kb": 420000, "system_ram_available_kb": 1200000,
               "native_heap_allocated_kb": 150000, "process_thread_count": 48,
               "thermal_status_label": "light"}),
        record("render_window", metrics={
            "game_renderer": common_stats,
            "level_renderer": common_stats,
            "level_id": "123", "level_name": "Test Level",
            "level_segment_index": 1
        }),
        record("dashcore_level_capture_diagnostics", attributes={
            "capture_enabled": capture_enabled, "frame_count": 100,
            "capture_skipped_frames": 0, "index_build_total_ms": 5.2,
            "capture_avg_ms": 0.1, "post_update_avg_ms": 1.1
        }),
        record("level_ended", attributes={"level_segment_index": 1, "level_id": "123",
               "level_name": "Test Level", "level_duration_ms": 30000}),
        record("session_end", attributes={"reason": "activity_stopped", "duration_ms": 40000,
               "game_frame_count_total": 1000, "level_frame_count_total": 100}),
    ]


def encode(records):
    return ("\n".join(json.dumps(item) for item in records) + "\n").encode("utf-8")


class LauncherDiagnosticsAnalyzerTests(unittest.TestCase):
    def test_reads_real_schema_and_aggregates_windows(self):
        result = analyzer.read_session(encode(sample_session()), "sample.jsonl")
        self.assertTrue(result["complete"], result["warnings"])
        self.assertFalse(result["capture_enabled"])
        self.assertEqual(result["renderer"]["game"]["frame_count"], 100)
        self.assertAlmostEqual(result["renderer"]["level"]["frame_interval"]["weighted_average_ms"], 16.7)
        self.assertEqual(result["renderer"]["game"]["windows_with_max_interval_over_50ms"], 0)
        self.assertEqual(result["process"]["pss_kb"]["average"], 420000)

    def test_malformed_lines_are_reported_not_silently_ignored(self):
        data = encode(sample_session()) + b"{broken json\n"
        result = analyzer.read_session(data, "bad.jsonl")
        self.assertFalse(result["complete"])
        self.assertEqual(len(result["parse_errors"]), 1)
        self.assertTrue(any("malformed" in item for item in result["warnings"]))

    def test_off_to_on_comparison_requires_matching_device_game_display_and_level(self):
        baseline = analyzer.read_session(encode(sample_session(False, 16.7, 22.0)), "off.jsonl")
        candidate = analyzer.read_session(encode(sample_session(True, 17.5, 25.0)), "on.jsonl")
        result = analyzer.compare_sessions(baseline, candidate, same_route_confirmed=True)
        self.assertTrue(result["metadata_compatible"], result["checks"])
        self.assertEqual(result["status"], "metadata-compatible-not-route-verified")
        self.assertEqual(len(result["matched_levels"]), 1)
        self.assertAlmostEqual(
            result["matched_levels"][0]["frame_interval_avg_ms"]["delta_percent"],
            (17.5 - 16.7) / 16.7 * 100
        )
        self.assertTrue(any("StartPos" in item for item in result["manual_confirmation_required"]))


    def test_a_b_pair_is_not_controlled_without_manual_route_confirmation(self):
        baseline = analyzer.read_session(encode(sample_session(False)), "off.jsonl")
        candidate = analyzer.read_session(encode(sample_session(True)), "on.jsonl")
        result = analyzer.compare_sessions(baseline, candidate)
        self.assertFalse(result["metadata_compatible"])
        self.assertFalse(result["checks"]["same_level_route_user_confirmed"])

    def test_capture_variant_mismatch_is_not_marked_controlled(self):
        baseline = analyzer.read_session(encode(sample_session(True)), "on-a.jsonl")
        candidate = analyzer.read_session(encode(sample_session(True)), "on-b.jsonl")
        result = analyzer.compare_sessions(baseline, candidate)
        self.assertFalse(result["metadata_compatible"])
        self.assertFalse(result["checks"]["baseline_capture_off"])

    def test_zip_export_loads_multiple_jsonl_sessions_without_extracting_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sessions.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("session-a.jsonl", encode(sample_session(False)))
                archive.writestr("nested/session-b.jsonl", encode(sample_session(True)))
                archive.writestr("../ignored.txt", "not a diagnostic file")
            sessions = analyzer.load_inputs(path)
            self.assertEqual(len(sessions), 2)
            self.assertEqual({item["capture_enabled"] for item in sessions}, {False, True})


if __name__ == "__main__":
    unittest.main()
