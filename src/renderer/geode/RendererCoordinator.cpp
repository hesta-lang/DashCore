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
         << " memory_sample_count=" << report.memorySampleCount
         << " system_ram_total_bytes=" << report.memory.systemTotalBytes
         << " system_ram_available_bytes=" << report.memory.systemAvailableBytes
         << " system_memory_valid=" << (report.memory.hasSystemMemory ? "true" : "false")
         << " process_rss_bytes=" << report.memory.processRssBytes
         << " process_rss_valid=" << (report.memory.hasProcessRss ? "true" : "false")
         << " process_pss_bytes=" << report.memory.processPssBytes
         << " process_pss_valid=" << (report.memory.hasProcessPss ? "true" : "false")
         << " process_peak_rss_bytes=" << report.memory.processPeakRssBytes
         << " process_peak_rss_valid=" << (report.memory.hasProcessPeakRss ? "true" : "false")
         << " process_virtual_bytes=" << report.memory.processVirtualBytes
         << " process_virtual_valid=" << (report.memory.hasProcessVirtual ? "true" : "false")
         << " cgroup_memory_limit_bytes=" << report.memory.cgroupMemoryLimitBytes
         << " cgroup_memory_limit_valid=" << (report.memory.hasCgroupMemoryLimit ? "true" : "false")
         << " cgroup_memory_limit_unlimited=" << (report.memory.cgroupMemoryLimitUnlimited ? "true" : "false")
         << " cgroup_memory_current_bytes=" << report.memory.cgroupMemoryCurrentBytes
         << " cgroup_memory_current_valid=" << (report.memory.hasCgroupMemoryCurrent ? "true" : "false")
         << " rlimit_as_soft_bytes=" << report.memory.resourceLimits.addressSpace.softBytes
         << " rlimit_as_hard_bytes=" << report.memory.resourceLimits.addressSpace.hardBytes
         << " rlimit_as_valid=" << (report.memory.resourceLimits.addressSpace.available ? "true" : "false")
         << " rlimit_as_soft_unlimited=" << (report.memory.resourceLimits.addressSpace.softUnlimited ? "true" : "false")
         << " rlimit_as_hard_unlimited=" << (report.memory.resourceLimits.addressSpace.hardUnlimited ? "true" : "false")
         << " rlimit_rss_soft_bytes=" << report.memory.resourceLimits.residentSet.softBytes
         << " rlimit_rss_hard_bytes=" << report.memory.resourceLimits.residentSet.hardBytes
         << " rlimit_rss_valid=" << (report.memory.resourceLimits.residentSet.available ? "true" : "false")
         << " rlimit_rss_soft_unlimited=" << (report.memory.resourceLimits.residentSet.softUnlimited ? "true" : "false")
         << " rlimit_rss_hard_unlimited=" << (report.memory.resourceLimits.residentSet.hardUnlimited ? "true" : "false")
         << " memory_safety_reserve_bytes=" << report.memoryBudget.systemSafetyReserveBytes
         << " memory_headroom_bytes=" << report.memoryBudget.availableHeadroomBytes
         << " memory_budget_bytes=" << report.memoryBudget.budgetBytes
         << " memory_max_single_buffer_bytes=" << report.memoryBudget.maxSingleBufferBytes
         << " memory_budget_can_allocate=" << (report.memoryBudget.canAllocateBuffers ? "true" : "false")
         << " memory_scope=procfs_cgroup_v1_v2_rlimit_soft_hard_read_only_diagnostics";
    ::geode::log::info("{}", line.str());
}

} // namespace

void RendererCoordinator::refreshMemoryBudget() noexcept {
    m_memorySnapshot = core::queryProcessMemorySnapshot();
    m_memoryBudget = core::calculateRendererMemoryBudget(m_memorySnapshot);
    if (m_memorySampleCount != UINT64_MAX) ++m_memorySampleCount;
}

void RendererCoordinator::onLevelInit(PlayLayer* layer, bool captureEnabled) noexcept {
    ++m_generation;
    if (m_generation == 0) m_generation = 1;
    m_collector.reset(m_generation);
    m_source.reset(layer);
    m_snapshot = {};
    m_diagnostics.reset();
    m_frameIndex = 0;
    m_memorySampleCount = 0;
    refreshMemoryBudget();
    m_captureEnabled = captureEnabled;
    m_active = true;
}

void RendererCoordinator::onFrame(
    PlayLayer* layer,
    std::uint64_t originalPostUpdateNanoseconds
) noexcept {
    if (!m_active || !layer) return;

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
    // Procfs is sampled at level start and once per 600 frames, never per frame.
    if ((m_frameIndex % 600u) == 0u) refreshMemoryBudget();
}

void RendererCoordinator::onLevelExit() noexcept {
    if (m_active) {
        auto const report = summary();
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
    m_memorySnapshot = {};
    m_memoryBudget = {};
    m_memorySampleCount = 0;
    m_captureEnabled = false;
    m_active = false;
    m_frameIndex = 0;
}

} // namespace dashcore::renderer::geode
