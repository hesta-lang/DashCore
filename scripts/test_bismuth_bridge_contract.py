#!/usr/bin/env python3
"""Static contract for the read-only Bismuth Android bridge."""
from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class BismuthBridgeContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "src/bismuth_bridge/BismuthBridge.hpp").read_text(encoding="utf-8")
        cls.source = (ROOT / "src/bismuth_bridge/BismuthBridge.cpp").read_text(encoding="utf-8")
        cls.gles = (ROOT / "src/renderer/gles/GLESBackend.cpp").read_text(encoding="utf-8")
        cls.readme = (ROOT / "README.md").read_text(encoding="utf-8")
        cls.docs = (ROOT / "docs/BISMUTH_BRIDGE.md").read_text(encoding="utf-8")

    def test_bridge_exposes_only_borrowed_cocos_views(self):
        for symbol in (
            "enumerateObjects",
            "enumerateObjectSprites",
            "enumerateBatchLayers",
            "queryCurrentGlesCapabilities",
            "ObjectView",
            "SpriteView",
            "BatchLayerView",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, self.header)
        self.assertIn("valid only for the duration of the visitor call", self.header)
        self.assertIn("std::unordered_set<cocos2d::CCSprite*> visited", self.source)

    def test_bismuth_sprite_roles_and_order_are_present(self):
        for marker in (
            "m_glowSprite",
            "m_colorSprite",
            "m_colorZLayerRelated",
            "m_isObjectBlack",
            "m_isColorSpriteBlack",
            "getDontDraw()",
            "getZOrder() < 0",
            "getZOrder() >= 0",
            "CCAffineTransformConcat",
            "SpriteType::BlackChild",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.source)

    def test_batch_adapter_uses_existing_cocos_nodes(self):
        self.assertIn("parentForZLayer(", self.source)
        self.assertIn("typeinfo_cast<cocos2d::CCSpriteBatchNode*>", self.source)
        self.assertIn("getOrderOfArrival()", self.source)
        self.assertNotIn("addChild(", self.source)
        self.assertNotIn("setVisible(", self.source)
        self.assertNotIn("removeChild(", self.source)

    def test_live_gl_query_does_not_create_or_change_context(self):
        self.assertIn("eglGetCurrentContext()", self.gles)
        self.assertIn("glGetString(GL_VERSION)", self.gles)
        self.assertIn('eglGetProcAddress("glGetStringi")', self.gles)
        for forbidden in (
            "eglCreateContext",
            "eglDestroyContext",
            "eglMakeCurrent",
            "glBindBuffer",
            "glDrawArrays",
            "glDrawElements",
            "glDrawArraysIndirect",
            "glMultiDraw",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.source)

    def test_docs_state_vanilla_renderer_and_gles2_fallback(self):
        self.assertIn("original Cocos renderer", self.readme)
        self.assertIn("GLES 2", self.readme)
        self.assertIn("read-only", self.docs.lower())
        self.assertIn("must not advertise desktop GL compatibility", self.docs)
        self.assertIn("does not claim", self.docs)


if __name__ == "__main__":
    unittest.main()
