#!/usr/bin/env python3
"""Product contract for the Geode-native DashCore rollback build."""
from __future__ import annotations

import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NativeRendererContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        read = lambda path: (ROOT / path).read_text(encoding="utf-8")
        cls.hooks = read("src/renderer/geode/PlayLayerRendererHooks.cpp")
        cls.coordinator = read("src/renderer/geode/RendererCoordinator.cpp")
        cls.readme = read("README.md")
        cls.cmake = read("CMakeLists.txt")
        cls.module = read("src/renderer/geode/ExperimentalRendererModule.cpp")
        cls.metadata = json.loads(read("mod.json"))

    def test_package_is_alpha_and_capture_is_off(self):
        self.assertEqual(self.metadata["id"], "hesta.optgod")
        self.assertEqual(self.metadata["version"], "0.11.8-alpha")
        self.assertEqual(self.metadata["name"], "DashCore Native Renderer Diagnostics")
        self.assertFalse(self.metadata["settings"]["capture-object-diagnostics"]["default"])
        self.assertIn('"capture-object-diagnostics"', self.hooks)
        self.assertIn("project(optgod VERSION 0.11.8)", self.cmake)
        self.assertIn("0.11.8-alpha", self.module)

    def test_no_custom_gles_or_bismuth_backend_is_built(self):
        for marker in ("GLESv3", "GLES", "EGL", "GLESBackend", "bismuth_bridge", "android-port"):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, self.cmake)
                self.assertNotIn(marker, self.readme)
        forbidden_paths = (
            "src/renderer/gles/GLESBackend.cpp",
            "src/renderer/gles/GLESBackend.hpp",
            "src/renderer/core/GlesCapabilities.cpp",
            "src/renderer/core/GlesCapabilities.hpp",
            "src/bismuth_bridge/BismuthBridge.hpp",
            "src/bismuth_bridge/BismuthTriggerAdapter.cpp",
            "android-port/src/GLCapabilities.cpp",
        )
        for path in forbidden_paths:
            with self.subTest(path=path):
                self.assertFalse((ROOT / path).exists())

    def test_geode_cocos_renderer_is_the_only_visible_draw_path(self):
        self.assertIn("original Cocos renderer", self.readme)
        self.assertIn("only visible renderer", self.readme)
        self.assertNotIn("CCDirector::drawScene", self.hooks)
        self.assertNotIn("CCNode::visit", self.hooks)
        self.assertNotIn("CCSprite::draw", self.hooks)
        self.assertNotIn("CCSpriteBatchNode::draw", self.hooks)
        self.assertNotIn("glDraw", self.hooks + self.coordinator)
        self.assertNotIn("egl", (self.hooks + self.coordinator).lower())

    def test_hooks_call_original_post_update_before_cpu_diagnostics(self):
        self.assertIn("PlayLayer::postUpdate(dt)", self.hooks)
        self.assertIn("onFrame(this, updateNanoseconds)", self.hooks)
        self.assertLess(
            self.hooks.index("PlayLayer::postUpdate(dt)"),
            self.hooks.index("coordinator().onFrame(this, updateNanoseconds)"),
        )
        self.assertIn("m_diagnostics.record(sample)", self.coordinator)
        self.assertIn("if (m_captureEnabled)", self.coordinator)

    def test_no_trigger_hooks_or_gpu_upload_path(self):
        for marker in (
            "processMoveActions",
            "processRotationActions",
            "processFollowActions",
            "toggleGroup(",
            "glGenBuffers",
            "glBindBuffer",
            "glBufferData",
            "glDrawArrays",
            "glDrawElements",
        ):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, self.hooks + self.coordinator)
        self.assertIn("disabled by default", self.readme.lower())
        self.assertNotIn("improves FPS", self.readme)
        self.assertNotIn("increases FPS", self.readme)


if __name__ == "__main__":
    unittest.main()
