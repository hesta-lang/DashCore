#!/usr/bin/env python3
"""Unit tests for the gameplay-only AutoTest runtime-log validator."""
from __future__ import annotations
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "autotest_gameplay_diagnostics",
    ROOT / "scripts/autotest_gameplay_diagnostics.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class GameplayDiagnosticsAutoTest(unittest.TestCase):
    def test_complete_single_gameplay_diagnostic_log(self):
        sample = """
        setupHasCompleted; performance-diagnostics=OFF, render-pipeline-diagnostics-requested=true, render-pipeline-hooks-ready=true, static-culling-requested=false, static-culling-active=false.
        DashCore 0.10.10-alpha RENDER-PIPELINE-HOOKS: requested=true, ready=true.
        DashCore 0.10.10-alpha RENDER-PIPELINE FINAL: frames=180, timedDrawFrames=23, timingStride=8, drawSceneAvgUs=8000.
        DashCore 0.10.10-alpha RENDER-UPDATE: frames=180, avgPostUpdateUs=1200.
        DashCore 0.10.10-alpha RENDER-UPDATE-SUBPHASES: baseGameLayerUpdateHookReady=true, playLayerUpdateVisibilityHookReady=true.
        DashCore 0.10.10-alpha RENDER-UPDATE-BINS-NOTE: CPU duration buckets.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCDirector::drawScene, samples=180.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=PlayLayer::postUpdate, samples=180.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCNode::visit, samples=20.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCSprite::draw, samples=20.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCSpriteBatchNode::draw, samples=20.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCTextureAtlas::drawQuads, samples=20.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=GJBaseGameLayer::update (PlayLayer), samples=20.
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=PlayLayer::updateVisibility, samples=20.
        DashCore 0.10.10-alpha OBJECT-VISUAL-HIERARCHY: arrayEntries=100.
        DashCore 0.10.10-alpha OBJECT-SECTION-LAYOUT: sectionOuterSlots=10.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertTrue(result["all_diagnostics_present"], result)
        self.assertEqual(result["render_pipeline_frames"], 180)
        self.assertTrue(result["required_checks"]["static_culling_confirmed_off"])

    def test_missing_sample_is_incomplete(self):
        result = MODULE.inspect_runtime_log(
            "setupHasCompleted; static-culling-requested=false, static-culling-active=false."
        )
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["full_180_frame_sample"])

    def test_culling_must_be_confirmed_off(self):
        sample = """
        DashCore 0.10.10-alpha RENDER-PIPELINE-HOOKS: requested=true, ready=true.
        DashCore 0.10.10-alpha RENDER-PIPELINE FINAL: frames=180.
        DashCore 0.10.10-alpha RENDER-UPDATE:
        DashCore 0.10.10-alpha RENDER-UPDATE-SUBPHASES:
        DashCore 0.10.10-alpha RENDER-UPDATE-BINS-NOTE:
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCDirector::drawScene
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=PlayLayer::postUpdate
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCNode::visit
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCSprite::draw
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCSpriteBatchNode::draw
        DashCore 0.10.10-alpha RENDER-TIMING-BINS: callback=CCTextureAtlas::drawQuads
        DashCore 0.10.10-alpha OBJECT-VISUAL-HIERARCHY:
        DashCore 0.10.10-alpha OBJECT-SECTION-LAYOUT:
        static-culling-requested=true, static-culling-active=true.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["static_culling_confirmed_off"])


if __name__ == "__main__":
    unittest.main()
