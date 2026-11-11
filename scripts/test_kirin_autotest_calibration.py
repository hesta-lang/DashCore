#!/usr/bin/env python3
"""Unit tests for the physical CPU reference consumed by AutoTest."""
from __future__ import annotations

import unittest

from kirin_autotest_calibration import build_physical_cpu_reference


def make_profile():
    return {
        "profile_id": "huawei-y9s",
        "display_name": "Huawei Y9s",
        "device": {
            "manufacturer": "Huawei",
            "model": "Y9s",
            "soc": {"name": "HiSilicon Kirin 710F"},
        },
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


if __name__ == "__main__":
    unittest.main()
