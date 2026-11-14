#include "renderer/core/RendererDiagnostics.hpp"

#include <algorithm>
#include <limits>

namespace dashcore::renderer::core {
namespace {

void addSaturated(std::uint64_t& target, std::uint64_t value) noexcept {
    auto const maximum = std::numeric_limits<std::uint64_t>::max();
    target = value > maximum - target ? maximum : target + value;
}

std::uint64_t percentile95(
    std::array<std::uint64_t, RendererDiagnostics::kTimingSampleWindow> samples,
    std::size_t count
) noexcept {
    if (count == 0) return 0;
    std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
    auto const rank = (count * 95u + 99u) / 100u;
    return samples[rank == 0 ? 0 : rank - 1u];
}

void addCounters(CaptureCounters& target, CaptureCounters const& value) noexcept {
    addSaturated(target.nodesVisited, value.nodesVisited);
    addSaturated(target.rootObjects, value.rootObjects);
    addSaturated(target.candidateSprites, value.candidateSprites);
    addSaturated(target.acceptedSprites, value.acceptedSprites);
    addSaturated(target.invisibleSubtrees, value.invisibleSubtrees);
    addSaturated(target.rejectedFlags, value.rejectedFlags);
    addSaturated(target.rejectedBatched, value.rejectedBatched);
    addSaturated(target.rejectedMissingTexture, value.rejectedMissingTexture);
    addSaturated(target.rejectedUnuploadedTexture, value.rejectedUnuploadedTexture);
    addSaturated(target.rejectedBlend, value.rejectedBlend);
    addSaturated(target.rejectedQuad, value.rejectedQuad);
    addSaturated(target.rejectedTransform, value.rejectedTransform);
    addSaturated(target.rejectedDescriptor, value.rejectedDescriptor);
    addSaturated(target.duplicateNodeReferences, value.duplicateNodeReferences);
    addSaturated(target.depthLimitHits, value.depthLimitHits);
    addSaturated(target.nodeLimitHits, value.nodeLimitHits);
    addSaturated(target.childrenLimitHits, value.childrenLimitHits);
    addSaturated(target.sourceFailures, value.sourceFailures);
}

} // namespace

void RendererDiagnostics::reset() noexcept {
    m_totalFrames = 0;
    m_successfulFrames = 0;
    m_failedFrames = 0;
    m_totals = {};
    m_totalCaptureNanoseconds = 0;
    m_totalCollectionNanoseconds = 0;
    m_maximumCaptureNanoseconds = 0;
    m_maximumCollectionNanoseconds = 0;
    m_captureSamples = {};
    m_collectionSamples = {};
    m_timingSampleCount = 0;
    m_nextTimingSample = 0;
    m_lastSnapshotSprites = 0;
    m_maximumSnapshotSprites = 0;
    m_lastSnapshotCapacityBytes = 0;
    m_maximumSnapshotCapacityBytes = 0;
    m_lastUniqueTextureCount = 0;
    m_maximumUniqueTextureCount = 0;
}

void RendererDiagnostics::record(CaptureFrameSample const& sample) noexcept {
    addSaturated(m_totalFrames, 1);
    if (sample.succeeded) {
        addSaturated(m_successfulFrames, 1);
    } else {
        addSaturated(m_failedFrames, 1);
    }

    addCounters(m_totals, sample.counters);
    addSaturated(m_totalCaptureNanoseconds, sample.captureNanoseconds);
    addSaturated(m_totalCollectionNanoseconds, sample.collectionNanoseconds);

    m_maximumCaptureNanoseconds = std::max(
        m_maximumCaptureNanoseconds,
        sample.captureNanoseconds
    );
    m_maximumCollectionNanoseconds = std::max(
        m_maximumCollectionNanoseconds,
        sample.collectionNanoseconds
    );

    m_captureSamples[m_nextTimingSample] = sample.captureNanoseconds;
    m_collectionSamples[m_nextTimingSample] = sample.collectionNanoseconds;
    m_nextTimingSample = (m_nextTimingSample + 1u) % kTimingSampleWindow;
    if (m_timingSampleCount < kTimingSampleWindow) {
        ++m_timingSampleCount;
    }

    m_lastSnapshotSprites = sample.snapshotSprites;
    m_maximumSnapshotSprites = std::max(
        m_maximumSnapshotSprites,
        sample.snapshotSprites
    );
    m_lastSnapshotCapacityBytes = sample.snapshotCapacityBytes;
    m_maximumSnapshotCapacityBytes = std::max(
        m_maximumSnapshotCapacityBytes,
        sample.snapshotCapacityBytes
    );
    m_lastUniqueTextureCount = sample.counters.uniqueTextures;
    m_maximumUniqueTextureCount = std::max(
        m_maximumUniqueTextureCount,
        sample.counters.uniqueTextures
    );
}

RendererDiagnosticSummary RendererDiagnostics::summary() const noexcept {
    RendererDiagnosticSummary result{};
    result.totalFrames = m_totalFrames;
    result.successfulFrames = m_successfulFrames;
    result.failedFrames = m_failedFrames;
    result.totals = m_totals;
    result.maximumCaptureNanoseconds = m_maximumCaptureNanoseconds;
    result.maximumCollectionNanoseconds = m_maximumCollectionNanoseconds;
    result.timingWindowSamples = m_timingSampleCount;
    result.lastSnapshotSprites = m_lastSnapshotSprites;
    result.maximumSnapshotSprites = m_maximumSnapshotSprites;
    result.lastSnapshotCapacityBytes = m_lastSnapshotCapacityBytes;
    result.maximumSnapshotCapacityBytes = m_maximumSnapshotCapacityBytes;
    result.lastUniqueTextureCount = m_lastUniqueTextureCount;
    result.maximumUniqueTextureCount = m_maximumUniqueTextureCount;

    if (m_totalFrames != 0) {
        result.averageCaptureNanoseconds = m_totalCaptureNanoseconds / m_totalFrames;
        result.averageCollectionNanoseconds = m_totalCollectionNanoseconds / m_totalFrames;
    }
    result.p95CaptureNanoseconds = percentile95(m_captureSamples, m_timingSampleCount);
    result.p95CollectionNanoseconds = percentile95(m_collectionSamples, m_timingSampleCount);
    return result;
}

} // namespace dashcore::renderer::core
