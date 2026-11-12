#!/usr/bin/env python3
"""Static contract tests for DashCore's opt-in renderer instrumentation."""
from __future__ import annotations

import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RenderPipelineContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.metadata = json.loads((ROOT / "mod.json").read_text(encoding="utf-8"))

    def test_renderer_hooks_are_declared_and_instrumented(self):
        required = (
            "class $modify(OptGodCCNode, cocos2d::CCNode)",
            "class $modify(OptGodCCSprite, cocos2d::CCSprite)",
            "class $modify(OptGodCCSpriteBatchNode, cocos2d::CCSpriteBatchNode)",
            "class $modify(OptGodCCTextureAtlas, cocos2d::CCTextureAtlas)",
            "CCNode::visit",
            "CCSprite::draw",
            "CCSpriteBatchNode::draw",
            "CCTextureAtlas::drawQuads",
            "CCTextureAtlas::drawNumberOfQuads",
            "RENDER-PIPELINE-HOOKS",
            "RENDER-PIPELINE-NOTE",
        )
        for marker in required:
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

    def test_sample_is_bounded_and_timing_is_sampled(self):
        self.assertIn("kRenderPipelineSampleFrames = 180", self.source)
        self.assertIn("kRenderPipelineTimingStride = 8", self.source)
        self.assertIn("g_rendererProfile.frames < kRenderPipelineSampleFrames", self.source)

    def test_renderer_diagnostics_setting_defaults_off(self):
        setting = self.metadata["settings"]["render-pipeline-diagnostics"]
        self.assertIs(setting["default"], False)
        self.assertIn("180 gameplay frames", setting["description"])
        self.assertIn("does not change rendering", setting["description"])

    def test_original_renderer_calls_are_preserved(self):
        for call in (
            "CCNode::visit();",
            "CCSprite::draw();",
            "CCSpriteBatchNode::draw();",
            "CCTextureAtlas::drawQuads();",
            "CCTextureAtlas::drawNumberOfQuads(numberOfQuads);",
            "CCDirector::drawScene();",
        ):
            with self.subTest(call=call):
                self.assertIn(call, self.source)

    def test_culling_remains_default_off(self):
        self.assertIs(
            self.metadata["settings"]["experimental-render-culling"]["default"],
            False,
        )


if __name__ == "__main__":
    unittest.main()
