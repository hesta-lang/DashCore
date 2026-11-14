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
        DashCore 0.10.11-alpha RENDER-CALLBACK CCNode::visit: calls=900, timedCalls=113, inclusiveAvgUs=9.00, inclusiveTotalUs=1017, inclusiveMaxUs=30.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCSprite::draw: calls=50, timedCalls=7, inclusiveAvgUs=4.00, inclusiveTotalUs=28, inclusiveMaxUs=12.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCSpriteBatchNode::draw: calls=3, timedCalls=1, inclusiveAvgUs=20.00, inclusiveTotalUs=20, inclusiveMaxUs=20.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCTextureAtlas::drawQuads: calls=4, timedCalls=1, inclusiveAvgUs=15.00, inclusiveTotalUs=15, inclusiveMaxUs=15.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCTextureAtlas::drawNumberOfQuads: calls=4, timedCalls=1, inclusiveAvgUs=12.00, inclusiveTotalUs=12, inclusiveMaxUs=12.
        DashCore RENDER-CALLBACK-STATUS CCDirector::drawScene: state=ENABLED_NOT_OBSERVED, hookFound=true, hookEnabled=true, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS PlayLayer::postUpdate: state=RUNNING, hookFound=true, hookEnabled=true, calls=180, timedCalls=180.
        DashCore RENDER-CALLBACK-STATUS CCNode::visit: state=RUNNING, hookFound=true, hookEnabled=true, calls=900, timedCalls=113.
        DashCore RENDER-CALLBACK-STATUS CCSprite::draw: state=RUNNING, hookFound=true, hookEnabled=true, calls=50, timedCalls=7.
        DashCore RENDER-CALLBACK-STATUS CCSpriteBatchNode::draw: state=RUNNING, hookFound=true, hookEnabled=true, calls=3, timedCalls=1.
        DashCore RENDER-CALLBACK-STATUS CCTextureAtlas::drawQuads: state=RUNNING, hookFound=true, hookEnabled=true, calls=4, timedCalls=1.
        DashCore RENDER-CALLBACK-STATUS CCTextureAtlas::drawNumberOfQuads: state=RUNNING, hookFound=true, hookEnabled=true, calls=4, timedCalls=1.
        DashCore RENDER-CALLBACK-STATUS GJBaseGameLayer::update (PlayLayer): state=RUNNING, hookFound=true, hookEnabled=true, calls=23, timedCalls=23.
        DashCore RENDER-CALLBACK-STATUS PlayLayer::updateVisibility: state=RUNNING, hookFound=true, hookEnabled=true, calls=23, timedCalls=23.
        DashCore RENDER-CALLBACK-STATUS CCDirector::drawScene: state=RUNNING, hookFound=true, hookEnabled=true, calls=180, timedCalls=180.
        DashCore RENDER-CALLBACK-STATUS PlayLayer::postUpdate: state=RUNNING, hookFound=true, hookEnabled=true, calls=180, timedCalls=180.
        DashCore RENDER-CALLBACK-STATUS CCNode::visit: state=RUNNING, hookFound=true, hookEnabled=true, calls=900, timedCalls=113.
        DashCore RENDER-CALLBACK-STATUS CCSprite::draw: state=RUNNING, hookFound=true, hookEnabled=true, calls=50, timedCalls=7.
        DashCore RENDER-CALLBACK-STATUS CCSpriteBatchNode::draw: state=RUNNING, hookFound=true, hookEnabled=true, calls=3, timedCalls=1.
        DashCore RENDER-CALLBACK-STATUS CCTextureAtlas::drawQuads: state=RUNNING, hookFound=true, hookEnabled=true, calls=4, timedCalls=1.
        DashCore RENDER-CALLBACK-STATUS CCTextureAtlas::drawNumberOfQuads: state=RUNNING, hookFound=true, hookEnabled=true, calls=4, timedCalls=1.
        DashCore RENDER-CALLBACK-STATUS GJBaseGameLayer::update (PlayLayer): state=RUNNING, hookFound=true, hookEnabled=true, calls=23, timedCalls=23.
        DashCore RENDER-CALLBACK-STATUS PlayLayer::updateVisibility: state=RUNNING, hookFound=true, hookEnabled=true, calls=23, timedCalls=23.
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
        DashCore 0.10.11-alpha RENDER-CALLBACK CCNode::visit: calls=900, timedCalls=113, inclusiveAvgUs=9.00, inclusiveTotalUs=1017, inclusiveMaxUs=30.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCSprite::draw: calls=50, timedCalls=7, inclusiveAvgUs=4.00, inclusiveTotalUs=28, inclusiveMaxUs=12.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCSpriteBatchNode::draw: calls=3, timedCalls=1, inclusiveAvgUs=20.00, inclusiveTotalUs=20, inclusiveMaxUs=20.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCTextureAtlas::drawQuads: calls=4, timedCalls=1, inclusiveAvgUs=15.00, inclusiveTotalUs=15, inclusiveMaxUs=15.
        DashCore 0.10.11-alpha RENDER-CALLBACK CCTextureAtlas::drawNumberOfQuads: calls=4, timedCalls=1, inclusiveAvgUs=12.00, inclusiveTotalUs=12, inclusiveMaxUs=12.
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
        self.assertTrue(result["required_checks"]["renderer_draw_callback_timing_observed"])
        self.assertEqual(
            result["renderer_draw_callback_names_observed"],
            ["CCSprite::draw", "CCSpriteBatchNode::draw", "CCTextureAtlas::drawQuads",
             "CCTextureAtlas::drawNumberOfQuads"],
        )
        self.assertEqual(result["last_render_update_progress_frames"], 180)

    def test_zero_draw_callback_timing_is_incomplete(self):
        sample = """
        setupHasCompleted; performance-diagnostics=OFF, render-pipeline-diagnostics-requested=true, render-pipeline-hooks-ready=true, static-culling-requested=false, static-culling-active=false.
        DashCore RENDER-PIPELINE-HOOKS: requested=true, ready=true.
        DashCore RENDER-UPDATE-PROGRESS: postUpdateFrames=180, drawSceneFrames=0, sampleActive=false.
        DashCore RENDER-PIPELINE FINAL: frames=180, drawSceneFrames=0, postUpdateFrames=180, sampleCoverage=update-only, timedDrawFrames=0, timingStride=8, drawSceneAvgUs=0.00.
        DashCore RENDER-UPDATE: frames=180, avgPostUpdateUs=1200.
        DashCore RENDER-UPDATE-SUBPHASES: baseGameLayerUpdateHookReady=true, playLayerUpdateVisibilityHookReady=true.
        DashCore RENDER-UPDATE-BINS-NOTE: histogram ranges are mutually exclusive CPU-duration buckets.
        DashCore RENDER-TIMING-BINS: callback=PlayLayer::postUpdate, samples=180.
        DashCore RENDER-TIMING-BINS: callback=CCNode::visit, samples=0.
        DashCore RENDER-TIMING-BINS: callback=CCSprite::draw, samples=0.
        DashCore RENDER-TIMING-BINS: callback=CCSpriteBatchNode::draw, samples=0.
        DashCore RENDER-TIMING-BINS: callback=CCTextureAtlas::drawQuads, samples=0.
        DashCore RENDER-TIMING-BINS: callback=GJBaseGameLayer::update (PlayLayer), samples=20.
        DashCore RENDER-TIMING-BINS: callback=PlayLayer::updateVisibility, samples=20.
        DashCore RENDER-CALLBACK CCNode::visit: calls=0, timedCalls=0, inclusiveAvgUs=0.00, inclusiveTotalUs=0, inclusiveMaxUs=0.
        DashCore RENDER-CALLBACK CCSprite::draw: calls=0, timedCalls=0, inclusiveAvgUs=0.00, inclusiveTotalUs=0, inclusiveMaxUs=0.
        DashCore RENDER-CALLBACK CCSpriteBatchNode::draw: calls=0, timedCalls=0, inclusiveAvgUs=0.00, inclusiveTotalUs=0, inclusiveMaxUs=0.
        DashCore RENDER-CALLBACK CCTextureAtlas::drawQuads: calls=0, timedCalls=0, inclusiveAvgUs=0.00, inclusiveTotalUs=0, inclusiveMaxUs=0.
        DashCore RENDER-CALLBACK-STATUS CCDirector::drawScene: state=ENABLED_NOT_OBSERVED, hookFound=true, hookEnabled=true, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS PlayLayer::postUpdate: state=RUNNING, hookFound=true, hookEnabled=true, calls=180, timedCalls=180.
        DashCore RENDER-CALLBACK-STATUS CCNode::visit: state=ENABLED_NOT_OBSERVED, hookFound=true, hookEnabled=true, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS CCSprite::draw: state=ENABLED_NOT_OBSERVED, hookFound=true, hookEnabled=true, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS CCSpriteBatchNode::draw: state=ENABLED_NOT_OBSERVED, hookFound=true, hookEnabled=true, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS CCTextureAtlas::drawQuads: state=ENABLED_NOT_OBSERVED, hookFound=true, hookEnabled=true, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS CCTextureAtlas::drawNumberOfQuads: state=ABSENT, hookFound=false, hookEnabled=false, calls=0, timedCalls=0.
        DashCore RENDER-CALLBACK-STATUS GJBaseGameLayer::update (PlayLayer): state=RUNNING, hookFound=true, hookEnabled=true, calls=23, timedCalls=23.
        DashCore RENDER-CALLBACK-STATUS PlayLayer::updateVisibility: state=RUNNING, hookFound=true, hookEnabled=true, calls=23, timedCalls=23.
        DashCore OBJECT-VISUAL-HIERARCHY: arrayEntries=100.
        DashCore OBJECT-SECTION-LAYOUT: sectionOuterSlots=10.
        static-culling-requested=false, static-culling-active=false.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertFalse(result["all_diagnostics_present"])
        self.assertFalse(result["required_checks"]["renderer_draw_callback_timing_observed"])

    def test_callback_inventory_distinguishes_absent_and_running(self):
        sample = """
        DashCore RENDER-CALLBACK-STATUS CCNode::visit: state=RUNNING, hookFound=true, hookEnabled=true, calls=500, timedCalls=63.
        DashCore RENDER-CALLBACK-STATUS CCSprite::draw: state=ABSENT, hookFound=false, hookEnabled=false, calls=0, timedCalls=0.
        """
        result = MODULE.inspect_runtime_log(sample)
        self.assertEqual(result["callback_inventory"]["CCNode::visit"]["state"], "RUNNING")
        self.assertEqual(result["callback_inventory"]["CCSprite::draw"]["state"], "ABSENT")
        self.assertIn("CCSprite::draw", result["callbacks_absent"])
        self.assertFalse(result["required_checks"]["callback_inventory_complete"])

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
