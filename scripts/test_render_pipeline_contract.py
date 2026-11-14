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
            "GJBaseGameLayer::update",
            "PlayLayer::updateVisibility",
            "RENDER-UPDATE-SUBPHASES",
            "RENDER-TIMING-BINS",
            "RENDER-UPDATE-BINS-NOTE",
            "recordTimingBucket",
            "postUpdateDurationBuckets",
            "drawSceneDurationBuckets",
            "RENDER-PIPELINE-HOOKS",
            "RENDER-PIPELINE-NOTE",
            "PlayLayer::postUpdate",
            "RENDER-UPDATE",
            "recordRenderPipelineUpdate",
        )
        for marker in required:
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

    def test_sample_is_bounded_and_timing_is_sampled(self):
        self.assertIn("kRenderPipelineSampleFrames = 180", self.source)
        self.assertIn("kRenderPipelineTimingStride = 8", self.source)
        self.assertIn("g_rendererProfile.frames < kRenderPipelineSampleFrames", self.source)

    def test_duration_histograms_have_mutually_exclusive_ranges(self):
        for marker in (
            "us0_250", "us251_500", "us501_1000", "us1001_2000",
            "us2001_4000", "us4001_8000", "usOver8000",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

    def test_renderer_diagnostics_setting_defaults_off(self):
        setting = self.metadata["settings"]["render-pipeline-diagnostics"]
        self.assertIs(setting["default"], False)
        self.assertIn("180 gameplay frames", setting["description"])
        self.assertIn("does not change rendering", setting["description"])

    def test_update_timing_is_bounded_to_the_renderer_sample(self):
        self.assertIn("updateFrames < kRenderPipelineSampleFrames", self.source)
        self.assertIn("kRenderPipelineSampleFrames = 180", self.source)
        self.assertIn("void finishRendererPipelineSampleIfReady()", self.source)
        self.assertIn("g_rendererProfile.updateFrames < kRenderPipelineSampleFrames", self.source)
        self.assertIn('sampleCoverage={}', self.source)
        self.assertIn("RENDER-PIPELINE-NOTE", self.source)
        self.assertIn("void postUpdate(float dt)", self.source)
        self.assertIn("avgPostUpdateUs", self.source)
        self.assertIn("postUpdateReady", self.source)

    def test_object_section_census_is_bounded_and_read_only(self):
        required = (
            "struct ObjectSectionLayoutStats",
            "m_activeObjects.size()",
            "m_activeObjectsCount",
            "m_activeObjectsIndex",
            "m_sections.size()",
            "kMaxObjectSectionOuterSlots = 1024",
            "kMaxObjectSectionCellSlots = 32768",
            "OBJECT-SECTION-LAYOUT",
            "OBJECT-SECTION-NOTE",
            "OBJECT-SECTION-ROWS",
            "outerSlotsWithCellSlots",
            "fullyScannedOuterSlots",
            "partiallyScannedOuterSlots",
            "outerSlotsWithObjectReferences",
            "maxCellsPerOuterSlot",
            "maxObjectReferencesPerOuterSlot",
            "reportObjectSectionLayout(g_profile.layer);",
        )
        for marker in required:
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

        start = self.source.index("void reportObjectSectionLayout")
        end = self.source.index("void recordRenderPipelineUpdate", start)
        helper = self.source[start:end]
        for forbidden in (
            "setVisible(",
            "setPosition(",
            "setScale(",
            "addChild(",
            "removeChild(",
            "visit();",
            "draw();",
            "->set",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, helper)

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

    def test_update_subphase_hooks_are_optional_and_sampled(self):
        required = (
            "g_baseGameLayerUpdateHook = registerOptionalHook",
            "g_playLayerVisibilityHook = registerOptionalHook",
            "baseGameLayerUpdate.calls % kRenderPipelineTimingStride == 0",
            "playLayerUpdateVisibility.calls % kRenderPipelineTimingStride == 0",
            "GJBaseGameLayer::update(dt);",
            "PlayLayer::updateVisibility(dt);",
            "g_baseGameLayerUpdateHookReady = ensureHook",
            "g_playLayerVisibilityHookReady = ensureHook",
            "RENDER-UPDATE-SUBPHASES",
        )
        for marker in required:
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

        # postUpdate controls only the bounded sample lifecycle. No renderer
        # callback is allowed to gate or disable any other renderer callback.
        self.assertIn("g_renderPipelineHooksReady = postUpdateReady;", self.source)
        readiness = self.source.rsplit("g_renderPipelineHooksReady =", 1)[1].split(";", 1)[0]
        self.assertEqual(readiness.strip(), "postUpdateReady")
        self.assertNotIn("drawSceneReady", readiness)
        self.assertNotIn("nodeReady", readiness)
        self.assertNotIn("atlasQuadsReady", readiness)

    def test_renderer_callbacks_are_sampled_independently_of_director(self):
        self.assertNotIn("sampleThisFrame", self.source)
        for marker in (
            "g_rendererProfile.nodeVisit.calls % kRenderPipelineTimingStride == 0",
            "g_rendererProfile.spriteDraw.calls % kRenderPipelineTimingStride == 0",
            "g_rendererProfile.batchDraw.calls % kRenderPipelineTimingStride == 0",
            "g_rendererProfile.atlasDrawQuads.calls % kRenderPipelineTimingStride == 0",
            "g_rendererProfile.atlasDrawNumberOfQuads.calls % kRenderPipelineTimingStride == 0",
            "bool const timedDrawFrame = rendererFrame &&",
            "RENDER-CALLBACK {}: calls={}, timedCalls={}",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

    def test_culling_remains_default_off(self):
        self.assertIs(
            self.metadata["settings"]["experimental-render-culling"]["default"],
            False,
        )


    def test_object_visual_hierarchy_scan_is_present_and_bounded(self):
        required = (
            "struct ObjectVisualHierarchyStats",
            "PlayLayer::m_objects",
            "getChildrenCount()",
            "getChildren()",
            "CCArrayExt<GameObject, false>",
            "typeinfo_cast<cocos2d::CCSprite*>",
            "OBJECT-VISUAL-HIERARCHY",
            "OBJECT-VISUAL-BUCKETS",
            "OBJECT-VISUAL-SAMPLE",
            "OBJECT-VISUAL-NOTE",
            "kMaxObjectVisualNodesPerObject = 4096",
            "kMaxObjectVisualDepth = 64",
            "kMaxTotalObjectVisualNodes = 250000",
            "reportObjectVisualHierarchy(g_profile.layer);",
        )
        for marker in required:
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

    def test_object_visual_hierarchy_helper_is_read_only(self):
        start = self.source.index("void reportObjectVisualHierarchy")
        end = self.source.index("void reportRendererPipeline", start)
        helper = self.source[start:end]
        for forbidden in (
            "setVisible(",
            "setPosition(",
            "setScale(",
            "addChild(",
            "removeChild(",
            "visit();",
            "draw();",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, helper)


    def test_diagnostic_release_version_and_defaults(self):
        self.assertEqual(self.metadata["version"], "0.10.14-alpha")
        self.assertIs(self.metadata["settings"]["render-pipeline-diagnostics"]["default"], False)
        self.assertIs(self.metadata["settings"]["experimental-render-culling"]["default"], False)
        self.assertIn("does not change rendering", self.metadata["settings"]["render-pipeline-diagnostics"]["description"])


    def test_missing_renderer_callback_does_not_disable_other_callbacks(self):
        self.assertIn("g_renderPipelineHooksReady = postUpdateReady;", self.source)
        self.assertNotIn("g_renderPipelineHooksReady = nodeReady &&", self.source)
        self.assertNotIn("disableIfEnabled(g_spriteDrawHook)", self.source)
        self.assertNotIn("disableIfEnabled(g_batchDrawHook)", self.source)
        self.assertIn("RENDER-CALLBACK-STATUS", self.source)
        self.assertIn('"RUNNING"', self.source)
        self.assertIn('"ABSENT"', self.source)

if __name__ == "__main__":
    unittest.main()
