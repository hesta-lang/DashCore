#!/usr/bin/env python3
"""Static safety contract for the CPU-only synthetic object grouping stage."""
from __future__ import annotations

import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ObjectGroupingContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        read = lambda path: (ROOT / path).read_text(encoding="utf-8")
        cls.metadata = json.loads(read("mod.json"))
        cls.header = read("src/renderer/core/ObjectGrouping.hpp")
        cls.source = read("src/renderer/core/ObjectGrouping.cpp")
        cls.hooks = read("src/renderer/geode/PlayLayerRendererHooks.cpp")
        cls.workflow = read(".github/workflows/android64-object-grouping-build.yml")

    def test_version_and_diagnostic_are_alpha_and_disabled(self):
        self.assertEqual(self.metadata["version"], "0.16.0-alpha")
        setting = self.metadata["settings"]["cpu-object-grouping-diagnostic"]
        self.assertFalse(setting["default"])
        self.assertIn("synthetic", setting["description"].lower())
        self.assertIn('getSettingValue<bool>("cpu-object-grouping-diagnostic")', self.hooks)
        self.assertIn("runSyntheticObjectGroupingDiagnostic()", self.hooks)
        self.assertIn("DashCore_CPU_OBJECT_GROUPING", self.hooks)

    def test_grouping_covers_required_fields_and_stable_groups(self):
        for field in ("positionsXY", "rotationsDegrees", "scalesXY",
                      "typeIds", "objectIds", "stateFlags", "groups"):
            self.assertIn(field, self.header)
        self.assertIn("kMaximumGroupedObjects = 4096", self.header)
        self.assertIn("kSyntheticGroupingObjectCount = 1024", self.header)
        self.assertIn("left < right", self.source)
        self.assertIn("validateGroupedObjectArrays(next)", self.source)
        self.assertIn("DuplicateObjectId", self.source)
        self.assertIn("NonFiniteTransform", self.source)

    def test_live_level_and_gl_paths_are_not_used(self):
        self.assertNotIn("PlayLayer", self.source)
        self.assertNotIn("m_objects", self.source)
        self.assertNotIn("glDraw", self.source)
        self.assertNotIn("glDispatchCompute", self.source)
        self.assertIn("runCpuObjectGroupingDiagnosticIfEnabled();", self.hooks)
        self.assertIn("target: Android64", self.workflow)

    def test_android64_workflow_uploads_geode_artifact(self):
        self.assertIn("geode-sdk/build-geode-mod@main", self.workflow)
        self.assertIn("Android64", self.workflow)
        self.assertIn("actions/upload-artifact@v4", self.workflow)
        self.assertIn("*.geode", self.workflow)


if __name__ == "__main__":
    unittest.main()
