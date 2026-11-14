#!/usr/bin/env python3
"""Unit tests for the current DashCore per-level capture log validator."""
from __future__ import annotations

import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "autotest_gameplay_diagnostics",
    ROOT / "scripts/autotest_gameplay_diagnostics.py",
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class GameplayDiagnosticsAutoTest(unittest.TestCase):
    def test_capture_summary_is_validated(self):
        sample = """
        [Geode] DashCore_CAPTURE_DIAGNOSTICS frames=1240 successful=1240 failed=0
        capture_enabled=true capture_performed_frames=1240 capture_skipped_frames=0
        index_build_total_ms=5.250 post_update_avg_ms=0.430 post_update_p95_last512_ms=0.780
        post_update_max_ms=2.100 capture_avg_ms=0.115 capture_p95_last512_ms=0.250
        capture_max_ms=1.200 collection_avg_ms=0.060 collection_p95_last512_ms=0.120
        p95_window_frames=512 nodes_visited_total=8500 object_array_index_builds_total=1
        object_array_entries_indexed_total=890 objects_cached_in_playlayer_tree_total=890
        objects_excluded_from_playlayer_tree_index_total=0 root_objects_total=890
        sprite_candidates_total=1200 sprites_accepted_total=50 invisible_subtrees_total=0
        rejected_flags_total=150 rejected_batched_total=100
        sprites_skipped_unsupported_flags_total=150 sprites_skipped_batch_node_total=100
        source_failures_total=0 memory_scope=snapshot_vector_capacity_only
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertTrue(result["all_diagnostics_present"], result)
        self.assertEqual(result["capture_summary"]["frames"], 1240)
        self.assertEqual(result["capture_summary"]["object_array_entries_indexed_total"], 890)
        self.assertEqual(result["capture_summary"]["capture_enabled"], "true")

    def test_capture_disabled_is_not_a_valid_capture_run(self):
        sample = """
        DashCore_CAPTURE_DIAGNOSTICS frames=100 successful=100 failed=0
        capture_enabled=false capture_performed_frames=0 capture_skipped_frames=100
        object_array_index_builds_total=0 object_array_entries_indexed_total=0
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["capture_setting_confirmed_on"])
        self.assertFalse(result["required_checks"]["read_only_capture_attempted"])

    def test_missing_summary_is_incomplete(self):
        result = MODULE.inspect_runtime_log(
            "Geometry Dash started; level entered; no DashCore summary"
        )
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["per_level_capture_summary_found"])

    def test_summary_with_no_indexed_objects_is_incomplete(self):
        sample = """
        DashCore_CAPTURE_DIAGNOSTICS frames=120 successful=120 failed=0
        capture_enabled=true capture_performed_frames=120 capture_skipped_frames=0
        object_array_index_builds_total=1 object_array_entries_indexed_total=0
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["level_objects_indexed"])


if __name__ == "__main__":
    unittest.main()
