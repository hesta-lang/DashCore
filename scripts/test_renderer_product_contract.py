#!/usr/bin/env python3
"""Product contract for the read-only DashCore experimental renderer."""
from __future__ import annotations

import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RendererProductContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.hooks = (ROOT / "src/renderer/geode/PlayLayerRendererHooks.cpp").read_text(encoding="utf-8")
        cls.adapter = (ROOT / "src/renderer/geode/GeodeLevelObjectSource.cpp").read_text(encoding="utf-8")
        cls.coordinator = (ROOT / "src/renderer/geode/RendererCoordinator.cpp").read_text(encoding="utf-8")
        cls.diagnostics_header = (ROOT / "src/renderer/core/RendererDiagnostics.hpp").read_text(encoding="utf-8")
        cls.diagnostics_source = (ROOT / "src/renderer/core/RendererDiagnostics.cpp").read_text(encoding="utf-8")
        cls.metadata = json.loads((ROOT / "mod.json").read_text(encoding="utf-8"))
        cls.readme = (ROOT / "README.md").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.module = (ROOT / "src/renderer/geode/ExperimentalRendererModule.cpp").read_text(encoding="utf-8")

    def test_package_identity_and_diagnostic_prerelease(self):
        self.assertEqual(self.metadata["id"], "hesta.optgod")
        self.assertEqual(self.metadata["version"], "0.11.4-alpha")
        self.assertEqual(self.metadata["name"], "DashCore Experimental Renderer")
        self.assertFalse(self.metadata["settings"]["capture-object-diagnostics"]["default"])
        self.assertIn('getSettingValue<bool>("capture-object-diagnostics")', self.hooks)
        self.assertIn("capture_enabled", self.hooks)
        self.assertIn("project(optgod VERSION 0.11.4)", self.cmake)
        self.assertIn("0.11.4-alpha", self.module)

    def test_legacy_probes_culling_and_draw_interception_are_absent(self):
        forbidden = (
            "StartupProfile",
            "RenderPipelineProfile",
            "CullingReasonTelemetry",
            "render-pipeline-diagnostics",
            "diagnostics-mode.txt",
            "static-culling-mode.txt",
            "load-last-level",
            "rememberLastPlayedLevel",
            "runGLESComputeProbeOnce",
            "runGLESObjectCullingProbeOnce",
            "AutoTest",
            "CCDirector::drawScene",
            "CCNode::visit",
            "CCSprite::draw",
            "CCSpriteBatchNode::draw",
            "CCTextureAtlas::drawQuads",
        )
        for marker in forbidden:
            with self.subTest(marker=marker):
                self.assertNotIn(marker, self.source)

    def test_live_capture_uses_only_update_lifecycle_and_never_draw_hooks(self):
        self.assertIn("#include <Geode/modify/PlayLayer.hpp>", self.hooks)
        self.assertIn("PlayLayer::postUpdate(dt)", self.hooks)
        self.assertIn("steady_clock::now()", self.hooks)
        self.assertIn("onFrame(this, updateNanoseconds)", self.hooks)
        self.assertIn("PlayLayer::onEnterTransitionDidFinish()", self.hooks)
        self.assertIn("PlayLayer::onExit()", self.hooks)
        self.assertLess(
            self.hooks.index("PlayLayer::postUpdate(dt)"),
            self.hooks.index("coordinator().onFrame(this, updateNanoseconds)"),
        )
        for forbidden in (
            "CCDirector::drawScene",
            "CCNode::visit",
            "CCSprite::draw",
            "CCSpriteBatchNode::draw",
            "CCTextureAtlas::drawQuads",
            "present(",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.hooks)

    def test_geode_source_is_bounded_conservative_and_instrumented(self):
        self.assertIn("m_layer->m_objects", self.adapter)
        self.assertIn("isVisibleDescendantOfPlayLayer", self.adapter)
        self.assertIn("current->getParent()", self.adapter)
        self.assertNotIn("object->getParent() != m_layer->m_objectLayer", self.adapter)
        self.assertIn("objectArrayEntries", self.adapter)
        self.assertIn("objectArrayIndexBuilds", self.adapter)
        self.assertIn("m_cachedRoots", self.adapter)
        self.assertIn("rebuildObjectIndex", self.adapter)
        self.assertIn("objectsInVisiblePlayLayerTree", self.adapter)
        self.assertIn("objectsOutsideVisiblePlayLayerTree", self.adapter)
        self.assertIn("node->getChildren()", self.adapter)
        self.assertIn("getZOrder()", self.adapter)
        self.assertIn("getOrderOfArrival()", self.adapter)
        self.assertIn("std::stable_sort", self.adapter)
        self.assertNotIn("sortAllChildren", self.adapter)
        self.assertIn("getBatchNode()", self.adapter)
        self.assertIn("m_bRectRotated", self.adapter)
        self.assertIn("m_bUseVertexMod", self.adapter)
        self.assertIn("m_bShouldBeHidden", self.adapter)
        self.assertIn("getName()", self.adapter)
        self.assertIn("kMaximumDepth", self.adapter)
        self.assertIn("kMaximumNodes", self.adapter)
        self.assertIn("candidateSprites", self.adapter)
        self.assertIn("uniqueTextures", self.adapter)
        self.assertIn("collectionNanoseconds", self.adapter)

    def test_diagnostics_are_bounded_and_logged_only_on_level_exit(self):
        self.assertIn("kTimingSampleWindow = 512", self.diagnostics_header)
        self.assertIn("std::array<std::uint64_t, kTimingSampleWindow>", self.diagnostics_header)
        self.assertIn("postUpdateNanoseconds", self.diagnostics_header)
        self.assertIn("m_diagnostics.record(sample)", self.coordinator)
        self.assertIn("if (m_captureEnabled)", self.coordinator)
        self.assertIn("capture_skipped_frames", self.coordinator)
        self.assertIn("index_build_total_ms", self.coordinator)
        self.assertIn("DashCore_CAPTURE_DIAGNOSTICS", self.coordinator)
        self.assertIn("post_update_avg_ms", self.coordinator)
        self.assertIn("object_array_entries_indexed_total", self.coordinator)
        self.assertIn("objects_cached_in_visible_playlayer_tree_total", self.coordinator)
        self.assertIn("objects_excluded_from_playlayer_tree_index_total", self.coordinator)
        self.assertIn("sprites_skipped_batch_node_total", self.coordinator)
        self.assertIn("memory_scope=snapshot_vector_capacity_only", self.coordinator)
        on_frame = self.coordinator.split("void RendererCoordinator::onFrame", 1)[1].split(
            "void RendererCoordinator::onLevelExit", 1
        )[0]
        self.assertNotIn("log::", on_frame)
        on_exit = self.coordinator.split("void RendererCoordinator::onLevelExit", 1)[1]
        self.assertIn("logLevelDiagnostics(report)", on_exit)
        self.assertIn("percentile95", self.diagnostics_source)


    def test_level_exit_bridge_is_idempotent(self):
        self.assertIn("if (renderCoordinator.active())", self.hooks)
        exit_hook = self.hooks.split("void onExit()", 1)[1]
        self.assertLess(
            exit_hook.index("if (renderCoordinator.active())"),
            exit_hook.index("PlayLayer::onExit()"),
        )
        self.assertIn("notifyLauncherLevelEnded()", exit_hook)
        self.assertIn("notifyLauncherLevelDiagnostics(report)", exit_hook)
        self.assertIn("renderCoordinator.onLevelExit()", exit_hook)

    def test_readme_describes_diagnostic_stage_without_optimization_claim(self):
        self.assertIn("0.11.4-alpha", self.readme)
        self.assertIn("read-only", self.readme.lower())
        self.assertIn("vanilla rendering", self.readme.lower())
        self.assertIn("latest 512 captures", self.readme)
        self.assertIn("A/B baseline", self.readme)
        self.assertIn("capture-object-diagnostics", self.readme)
        self.assertIn("not total process memory", self.readme.lower())
        self.assertIn("post-update time", self.readme.lower())
        self.assertIn("post-update time", self.readme.lower())
        self.assertNotIn("improves FPS", self.readme)
        self.assertNotIn("increases FPS", self.readme)
        self.assertNotIn("replaces the original renderer", self.readme.lower())


if __name__ == "__main__":
    unittest.main()
