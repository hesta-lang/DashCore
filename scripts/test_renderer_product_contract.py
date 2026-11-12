#!/usr/bin/env python3
"""Product contract for the read-only DashCore experimental renderer."""
from __future__ import annotations

import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RendererProductContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.metadata = json.loads((ROOT / "mod.json").read_text(encoding="utf-8"))
        cls.readme = (ROOT / "README.md").read_text(encoding="utf-8")

    def test_package_identity_and_prerelease(self):
        self.assertEqual(self.metadata["id"], "hesta.optgod")
        self.assertEqual(self.metadata["version"], "0.11.0-alpha")
        self.assertEqual(self.metadata["name"], "DashCore Experimental Renderer")
        self.assertFalse(self.metadata.get("settings"))

    def test_runtime_is_not_a_diagnostics_or_culling_product(self):
        forbidden = (
            "StartupProfile",
            "RenderPipelineProfile",
            "CullingReasonTelemetry",
            "render-pipeline-diagnostics",
            "diagnostics-mode.txt",
            "static-culling-mode.txt",
            "load-last-level",
            "rememberLastPlayedLevel",
            "runGLESComputeProbeOnce",
            "runGLESObjectCullingProbeOnce",
            "AutoTest",
            "CCDirector::drawScene",
            "CCNode::visit",
            "CCSprite::draw",
            "CCSpriteBatchNode::draw",
            "CCTextureAtlas::drawQuads",
        )
        for marker in forbidden:
            with self.subTest(marker=marker):
                self.assertNotIn(marker, self.source)

    def test_readme_describes_staged_renderer_without_optimization_claim(self):
        self.assertIn("experimental renderer", self.readme.lower())
        self.assertIn("read-only", self.readme.lower())
        self.assertIn("vanilla rendering", self.readme.lower())
        self.assertNotIn("FPS improvement", self.readme)
        self.assertNotIn("replaces the original renderer", self.readme.lower())


if __name__ == "__main__":
    unittest.main()
