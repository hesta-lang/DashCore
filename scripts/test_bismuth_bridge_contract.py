#!/usr/bin/env python3
"""Static safety/contract checks for the Bismuth Android bridge and trigger mirror."""
from __future__ import annotations
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class BismuthBridgeContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        read = lambda path: (ROOT / path).read_text(encoding="utf-8")
        cls.header = read("src/bismuth_bridge/BismuthBridge.hpp")
        cls.source = read("src/bismuth_bridge/BismuthBridge.cpp")
        cls.gles = read("src/renderer/gles/GLESBackend.cpp")
        cls.model_header = read("src/bismuth_bridge/BismuthGroupStateModel.hpp")
        cls.model_source = read("src/bismuth_bridge/BismuthGroupStateModel.cpp")
        cls.adapter_header = read("src/bismuth_bridge/BismuthTriggerAdapter.hpp")
        cls.adapter_source = read("src/bismuth_bridge/BismuthTriggerAdapter.cpp")
        cls.hooks = read("src/renderer/geode/PlayLayerRendererHooks.cpp")
        cls.readme = read("README.md")
        cls.docs = read("docs/BISMUTH_BRIDGE.md")
        cls.trigger_docs = read("docs/BISMUTH-TRIGGER-ADAPTER.md")
        cls.mod = read("mod.json")

    def test_bridge_exposes_borrowed_cocos_views(self):
        for symbol in ("enumerateObjects", "enumerateObjectSprites", "enumerateBatchLayers",
                       "queryCurrentGlesCapabilities", "ObjectView", "SpriteView", "BatchLayerView"):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, self.header)
        self.assertIn("valid only for the duration of the visitor call", self.header)
        self.assertIn("std::unordered_set<cocos2d::CCSprite*> visited", self.source)

    def test_sprite_roles_and_batch_adapter_preserve_bismuth_metadata(self):
        for marker in ("Base = 0", "Detail = 1", "Black = 2", "Glow = 3", "BlackChild = 4"):
            self.assertIn(marker, self.header)
        for marker in ("m_glowSprite", "m_colorSprite", "m_colorZLayerRelated",
                       "m_isObjectBlack", "m_isColorSpriteBlack", "getDontDraw()",
                       "getZOrder() < 0", "getZOrder() >= 0", "CCAffineTransformConcat",
                       "SpriteType::BlackChild", "parentForZLayer(",
                       "typeinfo_cast<cocos2d::CCSpriteBatchNode*>", "getOrderOfArrival()"):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)
        for forbidden in ("addChild(", "setVisible(", "removeChild("):
            self.assertNotIn(forbidden, self.source)

    def test_live_gl_query_does_not_create_or_change_context(self):
        self.assertIn("eglGetCurrentContext()", self.gles)
        self.assertIn("glGetString(GL_VERSION)", self.gles)
        self.assertIn('eglGetProcAddress("glGetStringi")', self.gles)
        for forbidden in ("eglCreateContext", "eglDestroyContext", "eglMakeCurrent",
                          "glBindBuffer", "glDrawArrays", "glDrawElements",
                          "glDrawArraysIndirect", "glMultiDraw"):
            self.assertNotIn(forbidden, self.source)

    def test_cpu_group_model_and_gpu_record_layout(self):
        for marker in ("GroupCombinationKey", "GroupCombinationState", "moveGroup",
                       "rotateGroup", "toggleGroup", "updateOpacities", "storeCheckpoint",
                       "restoreCheckpoint", "findCombinationIndex", "GpuGroupStateRecord",
                       "sizeof(GpuGroupStateRecord) == 64"):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.model_header + self.model_source)
        self.assertIn("BismuthGroupStateModel::makeGpuRecords", self.model_source)
        self.assertIn("objectCombinationIndices", self.adapter_header)
        self.assertIn("kNoCombination", self.adapter_header)

    def test_trigger_adapter_captures_vanilla_effect_state(self):
        for marker in ("observeMoveActions", "observeRotationActions", "observeFollowActions",
                       "observeToggleGroup", "captureVisualEffects", "opacityModForGroup",
                       "m_colorActionSpriteVector", "m_pulseEffectMap", "m_currentValue",
                       "storeCheckpoint", "restoreCheckpoint"):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.adapter_header + self.adapter_source)

    def test_vanilla_trigger_methods_are_still_called(self):
        expectations = (
            ("void processMoveActions()", "GJBaseGameLayer::processMoveActions();", "onMoveActions(this);"),
            ("void processRotationActions()", "onRotationActions(this);", "GJBaseGameLayer::processRotationActions();"),
            ("void processFollowActions()", "onFollowActions(this);", "GJBaseGameLayer::processFollowActions();"),
            ("void toggleGroup(int groupId, bool enabled)", "GJBaseGameLayer::toggleGroup(groupId, enabled);", "onToggleGroup(groupId, enabled);"),
        )
        for signature, before, after in expectations:
            with self.subTest(signature=signature):
                body = self.hooks.split(signature, 1)[1].split("\n    }", 1)[0]
                self.assertLess(body.index(before), body.index(after))

    def test_trigger_adapter_is_opt_in_and_has_no_draw_calls(self):
        self.assertIn('"enable-bismuth-trigger-state-adapter"', self.mod)
        self.assertIn('"default": false', self.mod)
        for source in (self.adapter_source, self.model_source, self.hooks):
            for forbidden in ("glDrawArrays(", "glDrawElements(", "CCSprite::draw", "CCNode::visit"):
                with self.subTest(forbidden=forbidden):
                    self.assertNotIn(forbidden, source)

    def test_docs_preserve_vanilla_renderer_and_gles2_boundary(self):
        self.assertIn("original Cocos renderer", self.readme)
        self.assertIn("GLES 2", self.readme)
        self.assertIn("read-only", self.docs.lower())
        self.assertIn("must not advertise desktop GL compatibility", self.docs)
        self.assertIn("does not claim", self.docs)
        self.assertIn("does not dispatch", self.trigger_docs)
        self.assertIn("not yet a Bismuth renderer", self.trigger_docs)
        self.assertIn("GLES 3.1", self.trigger_docs)
        self.assertIn("vanilla", self.trigger_docs.lower())


if __name__ == "__main__":
    unittest.main()
