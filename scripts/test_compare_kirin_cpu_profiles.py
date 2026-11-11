import unittest

from compare_kirin_cpu_profiles import build_comparison, render_markdown


class KirinCpuComparisonTests(unittest.TestCase):
    def setUp(self):
        self.reference = {
            "profile_id": "huawei-y9s",
            "cpu_execution_model": {
                "physical_measurements": {
                    "benchmark_id": "dashcore-kirin-cpu-v1",
                    "reference_sample_id": "idle",
                    "samples": [{
                        "sample_id": "idle",
                        "condition": "rested_idle",
                        "duration_ms": 4000,
                        "repetitions": 3,
                        "run_iterations_per_second": {
                            "higher_capacity_single_core": [100.0, 110.0, 120.0],
                            "all_allowed_cores_concurrent": [1000.0, 1100.0, 1200.0],
                        },
                    }],
                }
            },
        }
        self.cloud = {
            "benchmark_id": "dashcore-kirin-cpu-v1",
            "execution_mode": "adb_android_guest",
            "observed_device": {"model": "Pixel2"},
            "topology_assessment": {"status": "TOPOLOGY_NOT_VERIFIED"},
            "cpu_inventory": {
                "online_cpu_list": [0, 1, 2, 3],
                "process_allowed_cpu_list": [0, 1, 2, 3],
                "cpuset_restriction_detected": False,
                "cores": [],
            },
            "benchmarks": {
                "duration_ms": 4000,
                "repetitions": 3,
                "affinity_succeeded_for_all_workers": True,
                "median_per_cpu": {
                    "higher_capacity_single_core": {
                        "median_aggregate_iterations_per_second": 115.0,
                        "samples": 3,
                    },
                    "all_allowed_cores_concurrent": {
                        "median_aggregate_iterations_per_second": 1050.0,
                        "samples": 3,
                    },
                },
            },
        }

    def test_computes_reference_relative_comparison(self):
        report = build_comparison(self.reference, self.cloud)
        metrics = {item["test"]: item for item in report["metrics"]}
        self.assertEqual(metrics["higher_capacity_single_core"]["cloud_vs_physical_percent"], 4.55)
        self.assertEqual(metrics["all_allowed_cores_concurrent"]["cloud_vs_physical_percent"], -4.55)
        self.assertFalse(report["decision"]["cpu_budget_derived"])
        self.assertEqual(report["decision"]["status"], "COMPARISON_ONLY")

    def test_warns_when_cloud_has_too_few_repetitions(self):
        self.cloud["benchmarks"]["repetitions"] = 1
        report = build_comparison(self.reference, self.cloud)
        self.assertTrue(any("fewer than three" in item for item in report["warnings"]))

    def test_rejects_non_cloud_report(self):
        self.cloud["execution_mode"] = "local_android"
        with self.assertRaises(ValueError):
            build_comparison(self.reference, self.cloud)

    def test_markdown_states_comparison_limits(self):
        report = build_comparison(self.reference, self.cloud)
        markdown = render_markdown(report)
        self.assertIn("not a Geometry Dash FPS result", markdown)
        self.assertIn("No CPU budget", markdown)


if __name__ == "__main__":
    unittest.main()
