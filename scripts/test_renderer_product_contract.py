#!/usr/bin/env python3
"""Product contract for DashCore's independent Android renderer diagnostics."""
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
        cls.gles_header = read("src/renderer/core/GlesCapabilities.hpp")
        cls.gles_source = read("src/renderer/core/GlesCapabilities.cpp")
        cls.memory_header = read("src/renderer/core/MemoryBudget.hpp")
        cls.memory_source = read("src/renderer/core/MemoryBudget.cpp")
        cls.memory_probe = read("src/renderer/core/MemoryProbe.cpp")
        cls.compute_probe = read("src/renderer/geode/GlesComputeProbe.cpp")
        cls.compute_header = read("src/renderer/geode/GlesComputeProbe.hpp")
        cls.metadata = json.loads(read("mod.json"))

    def test_package_is_alpha_and_diagnostics_are_opt_in(self):
        self.assertEqual(self.metadata["id"], "hesta.optgod")
        self.assertEqual(self.metadata["version"], "0.12.0-alpha")
        self.assertEqual(self.metadata["name"], "DashCore Native Renderer Diagnostics")
        self.assertFalse(self.metadata["settings"]["capture-object-diagnostics"]["default"])
        self.assertFalse(self.metadata["settings"]["gles-compute-diagnostic-probe"]["default"])
        self.assertIn('"capture-object-diagnostics"', self.hooks)
        self.assertIn('"gles-compute-diagnostic-probe"', self.hooks)
        self.assertIn("project(optgod VERSION 0.12.0)", self.cmake)
        self.assertIn("0.12.0-alpha", self.module)

    def test_memory_budget_and_os_limit_diagnostics_are_read_only(self):
        self.assertIn("MemAvailable:", self.memory_source)
        self.assertIn("VmRSS:", self.memory_source)
        self.assertIn("VmSize:", self.memory_source)
        self.assertIn("Pss:", self.memory_source)
        self.assertIn("memory.max", self.memory_probe)
        self.assertIn("memory.current", self.memory_probe)
        self.assertIn("memory.limit_in_bytes", self.memory_probe)
        self.assertIn("memory.usage_in_bytes", self.memory_probe)
        self.assertIn("/proc/self/cgroup", self.memory_probe)
        self.assertIn("getrlimit", self.memory_probe)
        self.assertIn("RLIMIT_AS", self.memory_probe)
        self.assertIn("RLIMIT_RSS", self.memory_probe)
        self.assertIn("256ull * kMiB", self.memory_source)
        self.assertIn("snapshot.systemTotalBytes / 10ull", self.memory_source)
        self.assertIn("result.availableHeadroomBytes / 8ull", self.memory_source)
        self.assertIn("64ull * 1024ull * 1024ull", self.memory_header)
        self.assertIn("cgroup_memory_limit_bytes", self.coordinator)
        self.assertIn("rlimit_as_soft_bytes", self.coordinator)
        self.assertIn("rlimit_rss_hard_bytes", self.coordinator)
        self.assertIn("cgroup_memory_current_bytes", self.hooks)
        self.assertIn("rlimit_as_soft_bytes", self.hooks)
        self.assertIn("memory_scope=procfs_cgroup_v1_v2_rlimit_soft_hard_read_only_diagnostics", self.coordinator)
        self.assertIn("(m_frameIndex % 600u) == 0u", self.coordinator)
        self.assertNotIn("operator new", self.memory_probe)
        self.assertNotIn("malloc(", self.memory_probe)

    def test_compute_probe_is_one_shot_opt_in_and_tiny(self):
        setting = self.metadata["settings"]["gles-compute-diagnostic-probe"]
        self.assertFalse(setting["default"])
        self.assertIn('getSettingValue<bool>("gles-compute-diagnostic-probe")', self.hooks)
        self.assertIn("runGlesComputeProbeIfEnabled()", self.hooks)
        self.assertIn("static bool attempted = false", self.hooks)
        self.assertIn("attempted = true", self.hooks)
        self.assertIn("#version 310 es", self.compute_probe)
        self.assertIn("layout(local_size_x = 4)", self.compute_probe)
        self.assertIn("glDispatchCompute(1, 1, 1)", self.compute_probe)
        self.assertIn("GL_SHADER_STORAGE_BARRIER_BIT", self.compute_probe)
        self.assertIn("sizeof(expected)", self.compute_probe)
        self.assertIn("GL_MAP_READ_BIT", self.compute_probe)
        self.assertIn("GL_SHADER_STORAGE_BUFFER_BINDING", self.compute_probe)
        self.assertIn("glBindBufferBase", self.compute_probe)
        self.assertIn("glUseProgram", self.compute_probe)
        self.assertIn("outputValidated", self.compute_header)
        self.assertNotIn("glDraw", self.compute_probe)
        self.assertNotIn("PlayLayer", self.compute_probe)
        self.assertNotIn("m_objects", self.compute_probe)

    def test_no_bismuth_derived_or_replacement_render_backend(self):
        self.assertIn("if (ANDROID)", self.cmake)
        self.assertIn("GLESv3", self.cmake)
        self.assertNotIn("EGL", self.cmake)
        forbidden_paths = (
            "src/renderer/gles/GLESBackend.cpp",
            "src/renderer/gles/GLESBackend.hpp",
            "src/bismuth_bridge/BismuthBridge.hpp",
            "src/bismuth_bridge/BismuthTriggerAdapter.cpp",
            "android-port/src/GLCapabilities.cpp",
        )
        for path in forbidden_paths:
            with self.subTest(path=path):
                self.assertFalse((ROOT / path).exists())

    def test_original_cocos_renderer_remains_the_only_visible_draw_path(self):
        self.assertIn("original Cocos renderer", self.readme)
        self.assertIn("only visible draw path", self.readme)
        self.assertNotIn("CCDirector::drawScene", self.hooks)
        self.assertNotIn("CCNode::visit", self.hooks)
        self.assertNotIn("CCSprite::draw", self.hooks)
        self.assertNotIn("CCSpriteBatchNode::draw", self.hooks)
        self.assertNotIn("glDraw", self.hooks + self.coordinator + self.compute_probe)
        self.assertIn("glDispatchCompute", self.compute_probe)

    def test_gameplay_hooks_never_upload_or_draw_objects(self):
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
        self.assertIn("glGenBuffers", self.compute_probe)
        self.assertIn("glBufferData", self.compute_probe)
        self.assertIn("std::uint32_t expected[4]", self.compute_probe)
        self.assertIn("disabled by default", self.readme.lower())
        self.assertNotIn("improves FPS", self.readme)
        self.assertNotIn("increases FPS", self.readme)

    def test_hooks_measure_original_post_update_before_one_shot_probe(self):
        self.assertIn("PlayLayer::postUpdate(dt)", self.hooks)
        self.assertIn("onFrame(this, updateNanoseconds)", self.hooks)
        self.assertLess(
            self.hooks.index("PlayLayer::postUpdate(dt)"),
            self.hooks.index("coordinator().onFrame(this, updateNanoseconds)"),
        )
        self.assertLess(
            self.hooks.index("coordinator().onFrame(this, updateNanoseconds)"),
            self.hooks.index("runGlesComputeProbeIfEnabled();"),
        )
        self.assertIn("m_diagnostics.record(sample)", self.coordinator)
        self.assertIn("if (m_captureEnabled)", self.coordinator)

    def test_gles_capability_model_remains_pure_and_conservative(self):
        self.assertIn("GL_EXT_multi_draw_indirect", self.gles_source)
        self.assertIn("GL_EXT_disjoint_timer_query", self.gles_source)
        self.assertIn("GlesRenderPath::VanillaFallback", self.gles_source)
        self.assertIn("supportsSpirvSpecialization = false", self.gles_header)
        self.assertIn("supportsOpenClInterop = false", self.gles_header)
        forbidden = (
            "glGetString(",
            "glBindBuffer(",
            "glBufferData(",
            "glDrawElements(",
            "eglGetProcAddress(",
            "wglGetProcAddress(",
            "glSpecializeShader(",
            "#include <GLES",
            "#include <EGL",
        )
        combined = self.gles_header + self.gles_source
        for marker in forbidden:
            with self.subTest(marker=marker):
                self.assertNotIn(marker, combined)


if __name__ == "__main__":
    unittest.main()
