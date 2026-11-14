#include "renderer/geode/RendererCoordinator.hpp"

#include "renderer/geode/GeodeLevelObjectSource.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace dashcore::renderer::geode {
namespace {

double nanosecondsToMilliseconds(std::uint64_t nanoseconds) noexcept {
    return static_cast<double>(nanoseconds) / 1000000.0;
}

void logLevelDiagnostics(core::RendererDiagnosticSummary const& report) {
    std::ostringstream line;
    line << std::fixed << std::setprecision(3)
         << "DashCore_CAPTURE_DIAGNOSTICS"
         << " frames=" << report.totalFrames
         << " successful=" << report.successfulFrames
         << " failed=" << report.failedFrames
         << " capture_enabled=" << (report.captureEnabled ? "true" : "false")
         << " capture_performed_frames=" << report.capturePerformedFrames
         << " capture_skipped_frames=" << report.captureSkippedFrames
         << " index_build_total_ms=" << nanosecondsToMilliseconds(report.totalIndexBuildNanoseconds)
         << " post_update_avg_ms=" << nanosecondsToMilliseconds(report.averagePostUpdateNanoseconds)
         << " post_update_p95_last512_ms=" << nanosecondsToMilliseconds(report.p95PostUpdateNanoseconds)
         << " post_update_max_ms=" << nanosecondsToMilliseconds(report.maximumPostUpdateNanoseconds)
         << " capture_avg_ms=" << nanosecondsToMilliseconds(report.averageCaptureNanoseconds)
         << " capture_p95_last512_ms=" << nanosecondsToMilliseconds(report.p95CaptureNanoseconds)
         << " capture_max_ms=" << nanosecondsToMilliseconds(report.maximumCaptureNanoseconds)
         << " collection_avg_ms=" << nanosecondsToMilliseconds(report.averageCollectionNanoseconds)
         << " collection_p95_last512_ms=" << nanosecondsToMilliseconds(report.p95CollectionNanoseconds)
         << " collection_max_ms=" << nanosecondsToMilliseconds(report.maximumCollectionNanoseconds)
         << " p95_window_frames=" << report.timingWindowSamples
         << " nodes_visited_total=" << report.totals.nodesVisited
         << " object_array_index_builds_total=" << report.totals.objectArrayIndexBuilds
         << " object_array_entries_indexed_total=" << report.totals.objectArrayEntries
         << " objects_cached_in_playlayer_tree_total=" << report.totals.objectsInPlayLayerTree
         << " objects_excluded_from_playlayer_tree_index_total=" << report.totals.objectsOutsidePlayLayerTree
         << " root_objects_total=" << report.totals.rootObjects
         << " sprite_candidates_total=" << report.totals.candidateSprites
         << " sprites_accepted_total=" << report.totals.acceptedSprites
         << " invisible_subtrees_total=" << report.totals.invisibleSubtrees
         << " rejected_flags_total=" << report.totals.rejectedFlags
         << " rejected_batched_total=" << report.totals.rejectedBatched
         << " sprites_skipped_unsupported_flags_total=" << report.totals.rejectedFlags
         << " sprites_skipped_batch_node_total=" << report.totals.rejectedBatched
         << " rejected_missing_texture_total=" << report.totals.rejectedMissingTexture
         << " rejected_unuploaded_texture_total=" << report.totals.rejectedUnuploadedTexture
         << " rejected_blend_total=" << report.totals.rejectedBlend
         << " rejected_quad_total=" << report.totals.rejectedQuad
         << " rejected_transform_total=" << report.totals.rejectedTransform
         << " rejected_descriptor_total=" << report.totals.rejectedDescriptor
         << " duplicate_node_refs_total=" << report.totals.duplicateNodeReferences
         << " depth_limit_hits_total=" << report.totals.depthLimitHits
         << " node_limit_hits_total=" << report.totals.nodeLimitHits
         << " children_limit_hits_total=" << report.totals.childrenLimitHits
         << " source_failures_total=" << report.totals.sourceFailures
         << " unique_textures_last=" << report.lastUniqueTextureCount
         << " unique_textures_peak=" << report.maximumUniqueTextureCount
         << " snapshot_sprites_last=" << report.lastSnapshotSprites
         << " snapshot_sprites_peak=" << report.maximumSnapshotSprites
         << " snapshot_vector_capacity_bytes_last=" << report.lastSnapshotCapacityBytes
         << " snapshot_vector_capacity_bytes_peak=" << report.maximumSnapshotCapacityBytes
         << " memory_scope=snapshot_vector_capacity_only";
    ::geode::log::info("{}", line.str());
}

} // namespace

void RendererCoordinator::onLevelInit(
    PlayLayer* layer,
    bool captureEnabled,
    bool triggerAdapterEnabled
) noexcept {
    ++m_generation;
    if (m_generation == 0) m_generation = 1;
    m_collector.reset(m_generation);
    m_source.reset(layer);
    m_snapshot = {};
    m_diagnostics.reset();
    m_frameIndex = 0;
    m_captureEnabled = captureEnabled;
    m_triggerAdapterEnabled = triggerAdapterEnabled;
    m_active = true;

    if (m_triggerAdapterEnabled) {
        auto const initialized = layer &&
            m_triggerAdapter.initialize(layer, layer->m_objects);
        if (initialized) {
            ::geode::log::info(
                "DashCore Bismuth trigger adapter initialized: {} group combinations, {} maximum group ID; CPU reference state only.",
                m_triggerAdapter.model().combinationCount(),
                m_triggerAdapter.model().maximumGroupId()
            );
        } else {
            m_triggerAdapterEnabled = false;
            ::geode::log::warn(
                "DashCore Bismuth trigger adapter could not initialize; vanilla Geometry Dash trigger processing remains active."
            );
        }
    } else {
        m_triggerAdapter.reset();
    }
}

void RendererCoordinator::onFrame(
    PlayLayer* layer,
    std::uint64_t originalPostUpdateNanoseconds
) noexcept {
    if (!m_active || !layer) return;

    if (m_triggerAdapterEnabled)
        m_triggerAdapter.captureVisualEffects(layer->m_effectManager);

    core::CaptureFrameSample sample{};
    sample.postUpdateNanoseconds = originalPostUpdateNanoseconds;
    sample.capturePerformed = m_captureEnabled;

    if (m_captureEnabled) {
        auto const captureStart = std::chrono::steady_clock::now();
        bool captureSucceeded = false;

        try {
            core::RenderSnapshot next;
            captureSucceeded = m_collector.capture(m_source, m_frameIndex, next);
            if (captureSucceeded) {
                sample.snapshotSprites = next.sprites.size();
                sample.snapshotCapacityBytes =
                    next.sprites.capacity() * sizeof(core::SpriteDescriptor);
                m_snapshot = std::move(next);
            } else {
                m_snapshot = {};
            }
        } catch (...) {
            m_snapshot = {};
            captureSucceeded = false;
        }

        auto const captureElapsed = std::chrono::steady_clock::now() - captureStart;
        sample.captureNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(captureElapsed).count()
        );
        sample.collectionNanoseconds = m_source.lastStats().collectionNanoseconds;
        sample.indexBuildNanoseconds = m_source.lastStats().indexBuildNanoseconds;
        sample.counters = m_source.lastStats().counters;
        sample.succeeded = captureSucceeded;
    } else {
        // The A/B baseline still measures postUpdate and level boundaries without
        // scanning m_objects or building per-frame snapshots.
        m_snapshot = {};
    }

    m_diagnostics.record(sample);
    ++m_frameIndex;
}

void RendererCoordinator::onMoveActions(GJBaseGameLayer* layer) noexcept {
    if (m_active && m_triggerAdapterEnabled)
        m_triggerAdapter.observeMoveActions(layer);
}

void RendererCoordinator::onRotationActions(GJBaseGameLayer* layer) noexcept {
    if (m_active && m_triggerAdapterEnabled)
        m_triggerAdapter.observeRotationActions(layer);
}

void RendererCoordinator::onFollowActions(GJBaseGameLayer* layer) noexcept {
    if (m_active && m_triggerAdapterEnabled)
        m_triggerAdapter.observeFollowActions(layer);
}

void RendererCoordinator::onToggleGroup(std::int32_t groupId, bool enabled) noexcept {
    if (m_active && m_triggerAdapterEnabled)
        m_triggerAdapter.observeToggleGroup(groupId, enabled);
}

void RendererCoordinator::onLevelReset() noexcept {
    if (m_active && m_triggerAdapterEnabled)
        m_triggerAdapter.resetStates();
}

void RendererCoordinator::storeCheckpoint(CheckpointObject* checkpoint) {
    if (m_active && m_triggerAdapterEnabled)
        m_triggerAdapter.storeCheckpoint(checkpoint);
}

void RendererCoordinator::restoreCheckpoint(CheckpointObject* checkpoint) noexcept {
    if (m_active && m_triggerAdapterEnabled &&
        !m_triggerAdapter.restoreCheckpoint(checkpoint)) {
        ::geode::log::debug("DashCore trigger checkpoint state was not available; native checkpoint handling is unchanged.");
    }
}

void RendererCoordinator::onLevelExit() noexcept {
    if (m_active && m_triggerAdapterEnabled) {
        auto const& stats = m_triggerAdapter.stats();
        ::geode::log::info(
            "DashCore_BISMUTH_TRIGGER_DIAGNOSTICS move={} rotate={} follow={} toggle={} opacity_frames={} pulse_entries={} color_channel_samples={} group_combinations={}",
            stats.moveActions,
            stats.rotationActions,
            stats.followActions,
            stats.toggleActions,
            stats.opacityCaptures,
            stats.pulseCaptures,
            stats.colorChannelCaptures,
            m_triggerAdapter.model().combinationCount()
        );
    }

    if (m_active) {
        auto const report = m_diagnostics.summary();
        if (report.totalFrames != 0) {
            try {
                logLevelDiagnostics(report);
            } catch (...) {
                // Logging is diagnostic-only and must never interrupt level teardown.
            }
        }
    }

    m_snapshot = {};
    m_source.reset(nullptr);
    m_triggerAdapter.reset();
    m_captureEnabled = false;
    m_triggerAdapterEnabled = false;
    m_active = false;
    m_frameIndex = 0;
}

} // namespace dashcore::renderer::geode
