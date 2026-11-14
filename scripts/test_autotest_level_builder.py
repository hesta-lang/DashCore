#!/usr/bin/env python3
"""Unit tests for the generated DashCore AutoTest local level and save codec."""
from __future__ import annotations

import importlib.util
import pathlib
import plistlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_autotest_level", ROOT / "scripts/build_autotest_level.py"
)
assert SPEC and SPEC.loader
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class AutoTestLevelBuilderTests(unittest.TestCase):
    def test_generated_level_has_expected_density_profile(self):
        level, stats = builder.build_level()
        self.assertEqual(stats["floor_objects"], 240)
        self.assertEqual(stats["total_objects"], 890)
        self.assertEqual(stats["section_1_decorative_objects"], 20)
        self.assertEqual(stats["section_2_decorative_objects"], 156)
        self.assertEqual(stats["section_3_decorative_objects"], 474)
        self.assertEqual(level.count(";"), stats["total_objects"] + 1)

    def test_encrypted_save_round_trip_preserves_existing_levels(self):
        fixture = {
            "LLM_01": {
                "_isArr": True,
                "k_0": {
                    "kCEK": 4, "k2": "Existing player level",
                    "k4": "fixture", "k5": "Player", "k21": 2, "k50": 35,
                },
            },
            "LLM_02": 2200,
        }
        raw = builder.encode_save(
            plistlib.dumps(fixture, fmt=plistlib.FMT_XML, sort_keys=False)
        )
        decoded = builder.decode_save(raw)
        output, stats = builder.inject_level(decoded)
        result = plistlib.loads(output)
        self.assertEqual(result["LLM_01"]["k_0"]["k2"], builder.LEVEL_NAME)
        self.assertEqual(result["LLM_01"]["k_1"]["k2"], "Existing player level")
        self.assertEqual(stats["existing_levels_shifted"], 1)

        output_again, _ = builder.inject_level(output)
        repeated = plistlib.loads(output_again)
        names = [
            value.get("k2") for key, value in repeated["LLM_01"].items()
            if key.startswith("k_") and isinstance(value, dict)
        ]
        self.assertEqual(names.count(builder.LEVEL_NAME), 1)

    def test_corrupt_save_is_rejected(self):
        with self.assertRaises(ValueError):
            builder.decode_save(b"not-a-valid-save")


if __name__ == "__main__":
    unittest.main()
