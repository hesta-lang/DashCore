#!/usr/bin/env python3
"""Unit tests for the gameplay-only AutoTest runtime-log validator."""
from __future__ import annotations
import importlib.util
import pathlib
import unittest
from unittest.mock import patch

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
        DashCore 0.10.11-alpha RENDER-PIPELINE-HOOKS: requested=true, ready=true.
        DashCore 0.10.11-alpha RENDER-PIPELINE-PROGRESS: drawSceneFrames=30, postUpdateFrames=30, sampleActive=true.
        DashCore 0.10.11-alpha RENDER-UPDATE-PROGRESS: postUpdateFrames=30, drawSceneFrames=30, sampleActive=true.
        DashCore 0.10.11-alpha RENDER-PIPELINE FINAL: frames=180, timedDrawFrames=23, timingStride=8, drawSceneAvgUs=8000.
        DashCore 0.10.11-alpha RENDER-UPDATE: frames=180, avgPostUpdateUs=1200.
        DashCore 0.10.11-alpha RENDER-UPDATE-SUBPHASES: baseGameLayerUpdateHookReady=true, playLayerUpdateVisibilityHookReady=true.
        DashCore 0.10.11-alpha RENDER-UPDATE-BINS-NOTE: CPU duration buckets.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCDirector::drawScene, samples=180.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=PlayLayer::postUpdate, samples=180.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCNode::visit, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCSprite::draw, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCSpriteBatchNode::draw, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCTextureAtlas::drawQuads, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=GJBaseGameLayer::update (PlayLayer), samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=PlayLayer::updateVisibility, samples=20.
        DashCore 0.10.11-alpha OBJECT-VISUAL-HIERARCHY: arrayEntries=100.
        DashCore 0.10.11-alpha OBJECT-SECTION-LAYOUT: sectionOuterSlots=10.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertTrue(result["all_diagnostics_present"], result)
        self.assertEqual(result["render_pipeline_frames"], 180)
        self.assertTrue(result["required_checks"]["static_culling_confirmed_off"])

    def test_progress_markers_identify_each_callback_path(self):
        sample = """
        DashCore RENDER-PIPELINE-PROGRESS: drawSceneFrames=60, postUpdateFrames=60, sampleActive=true.
        DashCore RENDER-UPDATE-PROGRESS: postUpdateFrames=90, drawSceneFrames=60, sampleActive=true.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertTrue(result["required_checks"]["render_pipeline_progress_observed"])
        self.assertTrue(result["required_checks"]["render_update_progress_observed"])
        self.assertEqual(result["last_render_pipeline_progress_frames"], 60)
        self.assertEqual(result["last_render_update_progress_frames"], 90)

    def test_final_marker_can_be_found_in_logcat_when_geode_file_is_buffered(self):
        with patch.object(MODULE.runtime, "shell", side_effect=[
            "", "I Geode: DashCore RENDER-PIPELINE FINAL: frames=180"
        ]) as shell_mock:
            source = MODULE.runtime.find_render_pipeline_final_marker("emulator-1", "/logs")
        self.assertEqual(source, "logcat")
        self.assertEqual(shell_mock.call_count, 2)

    def test_geode_log_file_marker_takes_precedence(self):
        with patch.object(MODULE.runtime, "shell", return_value="/logs/Geode.log") as shell_mock:
            source = MODULE.runtime.find_render_pipeline_final_marker("emulator-1", "/logs")
        self.assertEqual(source, "geode-log-file")
        self.assertEqual(shell_mock.call_count, 1)

    def test_update_only_sample_is_complete_without_claiming_draw_coverage(self):
        sample = """
        setupHasCompleted; performance-diagnostics=OFF, render-pipeline-diagnostics-requested=true, render-pipeline-hooks-ready=true, static-culling-requested=false, static-culling-active=false.
        DashCore 0.10.11-alpha RENDER-PIPELINE-HOOKS: requested=true, ready=true.
        DashCore 0.10.11-alpha RENDER-UPDATE-PROGRESS: postUpdateFrames=180, drawSceneFrames=0, sampleActive=true.
        DashCore 0.10.11-alpha RENDER-PIPELINE FINAL: frames=180, drawSceneFrames=0, postUpdateFrames=180, sampleCoverage=update-only, timedDrawFrames=0, timingStride=8, drawSceneAvgUs=0.00.
        DashCore 0.10.11-alpha RENDER-UPDATE: frames=180, avgPostUpdateUs=1200.
        DashCore 0.10.11-alpha RENDER-UPDATE-SUBPHASES: baseGameLayerUpdateHookReady=true, playLayerUpdateVisibilityHookReady=true.
        DashCore 0.10.11-alpha RENDER-UPDATE-BINS-NOTE: histogram ranges are mutually exclusive CPU-duration buckets.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=PlayLayer::postUpdate, samples=180.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCNode::visit, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCSprite::draw, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCSpriteBatchNode::draw, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCTextureAtlas::drawQuads, samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=GJBaseGameLayer::update (PlayLayer), samples=20.
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=PlayLayer::updateVisibility, samples=20.
        DashCore 0.10.11-alpha OBJECT-VISUAL-HIERARCHY: arrayEntries=100.
        DashCore 0.10.11-alpha OBJECT-SECTION-LAYOUT: sectionOuterSlots=10.
        static-culling-requested=false, static-culling-active=false.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertTrue(result["all_diagnostics_present"], result)
        self.assertEqual(result["render_pipeline_frames"], 180)
        self.assertEqual(result["sample_coverage"], "update-only")
        self.assertFalse(result["draw_scene_callback_observed"])
        self.assertEqual(result["last_render_update_progress_frames"], 180)

    def test_missing_sample_is_incomplete(self):
        result = MODULE.inspect_runtime_log(
            "setupHasCompleted; static-culling-requested=false, static-culling-active=false."
        )
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["full_180_frame_sample"])

    def test_culling_must_be_confirmed_off(self):
        sample = """
        DashCore 0.10.11-alpha RENDER-PIPELINE-HOOKS: requested=true, ready=true.
        DashCore 0.10.11-alpha RENDER-PIPELINE FINAL: frames=180.
        DashCore 0.10.11-alpha RENDER-UPDATE:
        DashCore 0.10.11-alpha RENDER-UPDATE-SUBPHASES:
        DashCore 0.10.11-alpha RENDER-UPDATE-BINS-NOTE:
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCDirector::drawScene
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=PlayLayer::postUpdate
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCNode::visit
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCSprite::draw
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCSpriteBatchNode::draw
        DashCore 0.10.11-alpha RENDER-TIMING-BINS: callback=CCTextureAtlas::drawQuads
        DashCore 0.10.11-alpha OBJECT-VISUAL-HIERARCHY:
        DashCore 0.10.11-alpha OBJECT-SECTION-LAYOUT:
        static-culling-requested=true, static-culling-active=true.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["static_culling_confirmed_off"])


if __name__ == "__main__":
    unittest.main()
