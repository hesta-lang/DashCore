#include "renderer/geode/RendererCoordinator.hpp"

#include "renderer/geode/GeodeLevelObjectSource.hpp"

#include <Geode/Geode.hpp>

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
         << " root_objects_total=" << report.totals.rootObjects
         << " sprite_candidates_total=" << report.totals.candidateSprites
         << " sprites_accepted_total=" << report.totals.acceptedSprites
         << " invisible_subtrees_total=" << report.totals.invisibleSubtrees
         << " rejected_flags_total=" << report.totals.rejectedFlags
         << " rejected_batched_total=" << report.totals.rejectedBatched
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

void RendererCoordinator::onLevelInit() noexcept {
    ++m_generation;
    if (m_generation == 0) m_generation = 1;
    m_collector.reset(m_generation);
    m_snapshot = {};
    m_diagnostics.reset();
    m_frameIndex = 0;
    m_active = true;
}

void RendererCoordinator::onFrame(
    PlayLayer* layer,
    std::uint64_t originalPostUpdateNanoseconds
) noexcept {
    if (!m_active || !layer) return;

    auto const captureStart = std::chrono::steady_clock::now();
    GeodeLevelObjectSource source(layer);
    core::CaptureFrameSample sample{};
    sample.postUpdateNanoseconds = originalPostUpdateNanoseconds;
    bool captureSucceeded = false;

    try {
        core::RenderSnapshot next;
        captureSucceeded = m_collector.capture(source, m_frameIndex, next);
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
    sample.collectionNanoseconds = source.lastStats().collectionNanoseconds;
    sample.counters = source.lastStats().counters;
    sample.succeeded = captureSucceeded;
    m_diagnostics.record(sample);
    ++m_frameIndex;
}

void RendererCoordinator::onLevelExit() noexcept {
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
    m_active = false;
    m_frameIndex = 0;
}

} // namespace dashcore::renderer::geode
