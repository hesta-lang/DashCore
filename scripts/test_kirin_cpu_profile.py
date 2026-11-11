import importlib.util
import pathlib
import unittest

MODULE_PATH = pathlib.Path(__file__).with_name("kirin_cpu_profile.py")
SPEC = importlib.util.spec_from_file_location("kirin_cpu_profile", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class KirinCpuProfileTests(unittest.TestCase):
    def test_parse_cpu_list_ranges_and_duplicates(self):
        self.assertEqual(MODULE.parse_cpu_list("0-3,6,7"), [0, 1, 2, 3, 6, 7])
        self.assertEqual(MODULE.parse_cpu_list("3,1,3"), [1, 3])

    def test_parse_cpu_list_rejects_invalid_range(self):
        with self.assertRaises(ValueError):
            MODULE.parse_cpu_list("4-2")

    def test_parse_cpuinfo_identifies_arm_parts(self):
        rows = MODULE.parse_cpuinfo(
            "processor : 0\nCPU implementer : 0x41\nCPU part : 0xd03\n\n"
            "processor : 4\nCPU implementer : 0x41\nCPU part : 0xd09\n"
        )
        self.assertEqual(rows[0]["family"], "Cortex-A53")
        self.assertEqual(rows[1]["family"], "Cortex-A73")

    def test_frequency_grouping_is_marked_as_candidate(self):
        groups = MODULE.group_cpus([
            {"cpu": 0, "cpu_family": "unknown", "cpuinfo_max_freq_khz": 1700000},
            {"cpu": 1, "cpu_family": "unknown", "cpuinfo_max_freq_khz": 1700000},
            {"cpu": 4, "cpu_family": "unknown", "cpuinfo_max_freq_khz": 2200000},
            {"cpu": 5, "cpu_family": "unknown", "cpuinfo_max_freq_khz": 2200000},
        ])
        self.assertEqual(groups["method"], "cpuinfo_max_freq")
        self.assertEqual(groups["groups"][-1]["name"], "higher-frequency-candidate")


if __name__ == "__main__":
    unittest.main()
