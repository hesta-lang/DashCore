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
        cls.dynamic_probe = read("src/renderer/geode/GlesDynamicSsboProbe.cpp")
        cls.dynamic_header = read("src/renderer/geode/GlesDynamicSsboProbe.hpp")
        cls.transfer_benchmark = read("src/renderer/geode/GlesSsboTransferBenchmark.cpp")
        cls.transfer_benchmark_header = read("src/renderer/geode/GlesSsboTransferBenchmark.hpp")
        cls.async_transfer_benchmark = read("src/renderer/geode/GlesSsboAsyncTransferBenchmark.cpp")
        cls.async_transfer_benchmark_header = read("src/renderer/geode/GlesSsboAsyncTransferBenchmark.hpp")
        cls.metadata = json.loads(read("mod.json"))

    def test_package_is_alpha_and_diagnostics_are_opt_in(self):
        self.assertEqual(self.metadata["id"], "hesta.optgod")
        self.assertEqual(self.metadata["version"], "0.16.0-alpha")
        self.assertEqual(self.metadata["name"], "DashCore Native Renderer Diagnostics")
        self.assertFalse(self.metadata["settings"]["capture-object-diagnostics"]["default"])
        self.assertFalse(self.metadata["settings"]["gles-compute-diagnostic-probe"]["default"])
        self.assertFalse(self.metadata["settings"]["gles-dynamic-ssbo-diagnostic-probe"]["default"])
        self.assertIn('"capture-object-diagnostics"', self.hooks)
        self.assertIn('"gles-compute-diagnostic-probe"', self.hooks)
        self.assertIn('"gles-dynamic-ssbo-diagnostic-probe"', self.hooks)
        self.assertFalse(self.metadata["settings"]["gles-ssbo-legacy-sync-benchmark"]["default"])
        self.assertFalse(self.metadata["settings"]["gles-ssbo-async-transfer-benchmark"]["default"])
        self.assertIn('"gles-ssbo-legacy-sync-benchmark"', self.hooks)
        self.assertIn('"gles-ssbo-async-transfer-benchmark"', self.hooks)
        self.assertIn("project(optgod VERSION 0.16.0)", self.cmake)
        self.assertIn("0.16.0-alpha", self.module)

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
        self.assertIn("Never fall back to the cgroup root", self.memory_probe)
        self.assertIn("membershipPath[0] == '\\0'", self.memory_probe)
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
        self.assertIn("#undef glBindVertexArray", self.compute_probe)
        self.assertIn("#undef glDeleteVertexArrays", self.compute_probe)
        self.assertIn("#undef glGenVertexArrays", self.compute_probe)
        self.assertIn("#version 310 es", self.compute_probe)
        self.assertIn("layout(local_size_x = 4)", self.compute_probe)
        self.assertIn("glDispatchCompute(1, 1, 1)", self.compute_probe)
        self.assertIn("GL_SHADER_STORAGE_BARRIER_BIT", self.compute_probe)
        self.assertIn("sizeof(expected)", self.compute_probe)
        self.assertIn("GL_BUFFER_SIZE", self.compute_probe)
        self.assertIn("maxStorageBlock < 16", self.compute_probe)
        self.assertIn("GL_MAP_READ_BIT", self.compute_probe)
        self.assertIn("GL_SHADER_STORAGE_BUFFER_BINDING", self.compute_probe)
        self.assertIn("glBindBufferBase", self.compute_probe)
        self.assertIn("glUseProgram", self.compute_probe)
        self.assertIn("outputValidated", self.compute_header)
        self.assertNotIn("glDraw", self.compute_probe)
        self.assertNotIn("PlayLayer", self.compute_probe)
        self.assertNotIn("m_objects", self.compute_probe)

    def test_dynamic_ssbo_probe_is_opt_in_bounded_and_round_trips(self):
        setting = self.metadata["settings"]["gles-dynamic-ssbo-diagnostic-probe"]
        self.assertFalse(setting["default"])
        self.assertIn('getSettingValue<bool>("gles-dynamic-ssbo-diagnostic-probe")', self.hooks)
        self.assertIn("runGlesDynamicSsboProbeIfEnabled()", self.hooks)
        self.assertIn("DashCore_GLES_DYNAMIC_SSBO_PROBE", self.hooks)
        self.assertIn("static constexpr std::uint32_t batchSizes[] = {1u, 4u, 16u, 64u, 256u}", self.dynamic_probe)
        self.assertIn("glBufferData(GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_DRAW)", self.dynamic_probe)
        self.assertIn("glBufferSubData(", self.dynamic_probe)
        self.assertIn("GL_DYNAMIC_READ", self.dynamic_probe)
        self.assertIn("layout(std430, binding = 0) readonly buffer", self.dynamic_probe)
        self.assertIn("layout(std430, binding = 1) writeonly buffer", self.dynamic_probe)
        self.assertIn("glDispatchCompute(workGroupsX, 1, 1)", self.dynamic_probe)
        self.assertIn("GL_SHADER_STORAGE_BARRIER_BIT", self.dynamic_probe)
        self.assertIn("glMapBufferRange(", self.dynamic_probe)
        self.assertIn("glUnmapBuffer(", self.dynamic_probe)
        self.assertIn("previousInputStorageBuffer", self.dynamic_probe)
        self.assertIn("previousOutputStorageBuffer", self.dynamic_probe)
        self.assertIn("GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS", self.dynamic_probe)
        self.assertIn("maximumElements = 256u", self.dynamic_probe)
        self.assertIn("sizeof(std::uint32_t)", self.dynamic_probe)
        self.assertIn("maxBufferBytes = maximumBytes", self.dynamic_probe)
        self.assertIn("allBatchesValidated", self.dynamic_header)
        self.assertNotIn("glDraw", self.dynamic_probe)
        self.assertNotIn("PlayLayer", self.dynamic_probe)
        self.assertNotIn("m_objects", self.dynamic_probe)

    def test_ssbo_transfer_benchmark_is_opt_in_bounded_and_timed(self):
        setting = self.metadata["settings"]["gles-ssbo-legacy-sync-benchmark"]
        self.assertFalse(setting["default"])
        self.assertIn('getSettingValue<bool>("gles-ssbo-legacy-sync-benchmark")', self.hooks)
        self.assertIn("runGlesSsboTransferBenchmarkIfEnabled()", self.hooks)
        self.assertIn("DashCore_GLES_SSBO_BENCHMARK_SUMMARY", self.hooks)
        self.assertIn("payloadSizes[] = {", self.transfer_benchmark)
        for size in ("256u", "1024u", "4096u", "16384u"):
            with self.subTest(size=size):
                self.assertIn(size, self.transfer_benchmark)
        self.assertIn("repeatCount = 3u", self.transfer_benchmark)
        self.assertIn("GL_MAX_SHADER_STORAGE_BLOCK_SIZE", self.transfer_benchmark)
        self.assertIn("GL_MAX_COMPUTE_WORK_GROUP_COUNT", self.transfer_benchmark)
        self.assertIn("glBufferSubData(", self.transfer_benchmark)
        self.assertIn("glDispatchCompute(workGroupsX, 1, 1)", self.transfer_benchmark)
        self.assertIn("glMapBufferRange(", self.transfer_benchmark)
        self.assertIn("computeSubmitAverageMs", self.transfer_benchmark_header)
        self.assertIn("readbackMaximumMs", self.transfer_benchmark_header)
        self.assertIn("cycleAverageMs", self.transfer_benchmark_header)
        self.assertNotIn("glDraw", self.transfer_benchmark)
        self.assertNotIn("PlayLayer", self.transfer_benchmark)
        self.assertNotIn("m_objects", self.transfer_benchmark)


    def test_async_ssbo_benchmark_uses_zero_timeout_fences_and_is_frame_stepped(self):
        setting = self.metadata["settings"]["gles-ssbo-async-transfer-benchmark"]
        self.assertFalse(setting["default"])
        self.assertIn('getSettingValue<bool>("gles-ssbo-async-transfer-benchmark")', self.hooks)
        self.assertIn("tickGlesSsboAsyncTransferBenchmark(enabled)", self.hooks)
        self.assertIn("cancelGlesSsboAsyncTransferBenchmark();", self.hooks)
        self.assertIn("g_asyncSsboBenchmarkRequested", self.hooks)
        self.assertIn('getSettingValue<bool>("gles-ssbo-legacy-sync-benchmark")', self.hooks)
        self.assertIn("GLsync fence", self.async_transfer_benchmark)
        self.assertIn("glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)", self.async_transfer_benchmark)
        self.assertIn("glFlush()", self.async_transfer_benchmark)
        self.assertIn("glClientWaitSync(state.fence, 0, 0)", self.async_transfer_benchmark)
        self.assertIn("GL_TIMEOUT_EXPIRED", self.async_transfer_benchmark)
        self.assertIn("GL_ALREADY_SIGNALED", self.async_transfer_benchmark)
        self.assertIn("GL_CONDITION_SATISFIED", self.async_transfer_benchmark)
        self.assertIn("kMaximumFencePollFrames = 600u", self.async_transfer_benchmark)
        self.assertIn("glMapBufferRange(", self.async_transfer_benchmark)
        self.assertLess(
            self.async_transfer_benchmark.index("glClientWaitSync(state.fence, 0, 0)"),
            self.async_transfer_benchmark.index("glMapBufferRange("),
        )
        self.assertIn("averageFencePollFrames", self.async_transfer_benchmark_header)
        self.assertIn("endToEndAverageMs", self.async_transfer_benchmark_header)
        self.assertNotIn("glFinish(", self.async_transfer_benchmark)
        self.assertNotIn("GL_SYNC_FLUSH_COMMANDS_BIT", self.async_transfer_benchmark)
        self.assertNotIn("glDraw", self.async_transfer_benchmark)
        self.assertNotIn("PlayLayer", self.async_transfer_benchmark)
        self.assertNotIn("m_objects", self.async_transfer_benchmark)

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
        self.assertNotIn("glDraw", self.hooks + self.coordinator + self.compute_probe + self.dynamic_probe + self.transfer_benchmark + self.async_transfer_benchmark)
        self.assertIn("glDispatchCompute", self.compute_probe)
        self.assertIn("glDispatchCompute", self.transfer_benchmark)

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
        self.assertLess(
            self.hooks.index("runGlesComputeProbeIfEnabled();"),
            self.hooks.index("runGlesDynamicSsboProbeIfEnabled();"),
        )
        self.assertIn("runGlesSsboAsyncTransferBenchmarkIfEnabled();", self.hooks)
        self.assertIn("runGlesSsboTransferBenchmarkIfEnabled();", self.hooks)
        self.assertLess(
            self.hooks.index("runGlesDynamicSsboProbeIfEnabled();"),
            self.hooks.index("runGlesSsboAsyncTransferBenchmarkIfEnabled();"),
        )
        self.assertLess(
            self.hooks.index("runGlesSsboAsyncTransferBenchmarkIfEnabled();"),
            self.hooks.index("runGlesSsboTransferBenchmarkIfEnabled();"),
        )
        self.assertIn("cancelGlesSsboAsyncTransferBenchmark();", self.hooks)
        self.assertGreater(
            self.hooks.index("runGlesSsboTransferBenchmarkIfEnabled();"),
            self.hooks.index("runGlesDynamicSsboProbeIfEnabled();"),
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
