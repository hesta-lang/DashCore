#!/usr/bin/env python3
"""Unit tests for the physical CPU reference consumed by AutoTest."""
from __future__ import annotations

import importlib.util
import pathlib
import unittest

from kirin_autotest_calibration import build_physical_cpu_reference

AUTOTEST_PATH = pathlib.Path(__file__).with_name("autotest_ab.py")
AUTOTEST_SPEC = importlib.util.spec_from_file_location("autotest_ab", AUTOTEST_PATH)
AUTOTEST = importlib.util.module_from_spec(AUTOTEST_SPEC)
AUTOTEST_SPEC.loader.exec_module(AUTOTEST)


def make_profile():
    return {
        "profile_id": "huawei-y9s",
        "display_name": "Huawei Y9s",
        "device": {
            "manufacturer": "Huawei",
            "model": "Y9s",
            "soc": {"name": "HiSilicon Kirin 710F"},
        },
        "android": {"version": "10", "api_level": 29, "abi": "arm64-v8a"},
        "display": {"width_px": 2340, "height_px": 1080, "refresh_rate_hz": 60},
        "cpu_execution_model": {
            "calibration": {"minimum_repetitions": 3},
            "physical_measurements": {
                "benchmark_id": "dashcore-kirin-cpu-v1",
                "reference_sample_id": "idle",
                "device_identity": {
                    "manufacturer": "HUAWEI",
                    "model": "STK-LX3",
                    "soc_reported": "kirin710",
                },
                "core_inventory": {
                    "Cortex-A53": {"cpu_ids": [0, 1, 2, 3]},
                    "Cortex-A73": {"cpu_ids": [4, 5, 6, 7]},
                },
                "samples": [{
                    "sample_id": "idle",
                    "condition": "idle_after_rest",
                    "duration_ms": 4000,
                    "repetitions": 3,
                    "run_iterations_per_second": {
                        "higher_capacity_single_core": [197.0, 198.0, 197.5],
                        "lower_capacity_single_core": [137.0, 138.0, 136.0],
                        "all_allowed_cores_concurrent": [1330.0, 1340.0, 1335.0],
                    },
                }],
            },
        },
    }


class PhysicalCpuReferenceTests(unittest.TestCase):
    def test_culling_parser_reconciles_protected_player_counter(self):
        sample = """
        setupHasCompleted; diagnostics=OFF, static-culling-requested=true, static-culling-active=true.
        DashCore 0.10.4-alpha CULL-STATS frames=120, warmupFrames=600, visitedGameObjects=1000, eligibleObjects=500, culledObjects=12.
        DashCore 0.10.4-alpha CULL-REASONS visited=1000, eligible=500, protectedPlayer=10, protectedEffect=90, protectedTrigger=150, protectedGrouped=200, protectedRotating=50, missingDirector=0, contentSizeFallbacks=500, invalidBoundingRect=0, invalidAnchor=0, invalidWorldBounds=0, invalidViewport=0, withinGuardBand=488, offscreenX=4, offscreenY=5, offscreenBoth=3.
        DashCore 0.10.4-alpha CULL-NEAREST: found=true, guardClearance=2.50, objectID=42, bounds=(1.00,2.00)-(3.00,4.00).
        """
        result = AUTOTEST.parse_culling_runtime(sample)
        self.assertTrue(result["active_confirmed"], result)
        self.assertTrue(result["reason_report_found"], result)
        self.assertEqual(result["reason_counts"]["protected_player"], 10)
        self.assertEqual(result["protected_objects_total"], 500)
        self.assertEqual(result["eligible_reason_total"], 500)
        self.assertTrue(result["reason_totals_match"], result)
        self.assertEqual(result["reason_culled_total"], 12)

    def test_culling_parser_rejects_legacy_reason_line_without_player_field(self):
        sample = """
        setupHasCompleted; diagnostics=OFF, static-culling-requested=true, static-culling-active=true.
        DashCore 0.10.3-alpha CULL-STATS frames=120, warmupFrames=600, visitedGameObjects=1000, eligibleObjects=500, culledObjects=12.
        DashCore 0.10.3-alpha CULL-REASONS visited=1000, eligible=500, protectedEffect=100, protectedTrigger=150, protectedGrouped=200, protectedRotating=50, missingDirector=0, contentSizeFallbacks=500, invalidBoundingRect=0, invalidAnchor=0, invalidWorldBounds=0, invalidViewport=0, withinGuardBand=488, offscreenX=4, offscreenY=5, offscreenBoth=3.
        """
        result = AUTOTEST.parse_culling_runtime(sample)
        self.assertFalse(result["reason_report_found"], result)
        self.assertFalse(result["reason_totals_match"], result)

    def test_uses_selected_physical_sample_and_medians(self):
        result = build_physical_cpu_reference(make_profile())
        self.assertEqual(result["status"], "PHYSICAL_REFERENCE_CONTEXT_ONLY")
        self.assertEqual(result["reference_device"]["model"], "STK-LX3")
        self.assertEqual(result["selected_sample"]["sample_id"], "idle")
        self.assertEqual(
            result["median_iterations_per_second"]["higher_capacity_single_core"],
            197.5,
        )
        self.assertEqual(
            result["median_iterations_per_second"]["lower_capacity_single_core"],
            137.0,
        )
        self.assertFalse(result["interpretation"]["cpu_budget_derived"])
        self.assertFalse(result["interpretation"]["cloud_cpu_limits_applied"])
        self.assertFalse(result["interpretation"]["fps_normalization_applied"])

    def test_rejects_missing_benchmark_identity(self):
        profile = make_profile()
        profile["cpu_execution_model"]["physical_measurements"]["benchmark_id"] = "other"
        with self.assertRaises(ValueError):
            build_physical_cpu_reference(profile)

    def test_rejects_too_few_repetitions(self):
        profile = make_profile()
        sample = profile["cpu_execution_model"]["physical_measurements"]["samples"][0]
        sample["repetitions"] = 2
        with self.assertRaises(ValueError):
            build_physical_cpu_reference(profile)

    def test_rejects_invalid_throughput(self):
        profile = make_profile()
        sample = profile["cpu_execution_model"]["physical_measurements"]["samples"][0]
        sample["run_iterations_per_second"]["all_allowed_cores_concurrent"][1] = float("nan")
        with self.assertRaises(ValueError):
            build_physical_cpu_reference(profile)

    def test_autotest_assessment_embeds_physical_reference_without_emulation(self):
        profile = make_profile()
        actual_guest = {
            "android_api_level": "30",
            "primary_abi": "arm64-v8a",
            "supported_abis": "arm64-v8a,armeabi-v7a",
            "wm_size_reported_px": [1080, 1920],
        }
        assessment = AUTOTEST.assess_device_profile(profile, actual_guest)
        reference = assessment["cpu_calibration_reference"]
        self.assertEqual(assessment["hardware_equivalence"], "NOT_EMULATED")
        self.assertEqual(reference["selected_sample"]["sample_id"], "idle")
        self.assertEqual(
            reference["median_iterations_per_second"]["higher_capacity_single_core"],
            197.5,
        )
        self.assertFalse(reference["interpretation"]["cpu_budget_derived"])
        self.assertFalse(reference["interpretation"]["fps_normalization_applied"])


if __name__ == "__main__":
    unittest.main()
