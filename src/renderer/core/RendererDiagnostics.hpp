#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dashcore::renderer::core {

struct CaptureCounters {
    std::uint64_t nodesVisited = 0;
    std::uint64_t objectArrayEntries = 0;
    std::uint64_t objectsInVisiblePlayLayerTree = 0;
    std::uint64_t objectsOutsideVisiblePlayLayerTree = 0;
    std::uint64_t rootObjects = 0;
    std::uint64_t candidateSprites = 0;
    std::uint64_t acceptedSprites = 0;
    std::uint64_t invisibleSubtrees = 0;
    std::uint64_t rejectedFlags = 0;
    std::uint64_t rejectedBatched = 0;
    std::uint64_t rejectedMissingTexture = 0;
    std::uint64_t rejectedUnuploadedTexture = 0;
    std::uint64_t rejectedBlend = 0;
    std::uint64_t rejectedQuad = 0;
    std::uint64_t rejectedTransform = 0;
    std::uint64_t rejectedDescriptor = 0;
    std::uint64_t duplicateNodeReferences = 0;
    std::uint64_t depthLimitHits = 0;
    std::uint64_t nodeLimitHits = 0;
    std::uint64_t childrenLimitHits = 0;
    std::uint64_t sourceFailures = 0;
    std::uint64_t uniqueTextures = 0;
};

struct CaptureFrameSample {
    CaptureCounters counters{};
    std::uint64_t postUpdateNanoseconds = 0;
    std::uint64_t captureNanoseconds = 0;
    std::uint64_t collectionNanoseconds = 0;
    std::uint64_t snapshotSprites = 0;
    std::uint64_t snapshotCapacityBytes = 0;
    bool succeeded = false;
};

struct RendererDiagnosticSummary {
    std::uint64_t totalFrames = 0;
    std::uint64_t successfulFrames = 0;
    std::uint64_t failedFrames = 0;
    CaptureCounters totals{};
    std::uint64_t averagePostUpdateNanoseconds = 0;
    std::uint64_t p95PostUpdateNanoseconds = 0;
    std::uint64_t maximumPostUpdateNanoseconds = 0;
    std::uint64_t averageCaptureNanoseconds = 0;
    std::uint64_t p95CaptureNanoseconds = 0;
    std::uint64_t maximumCaptureNanoseconds = 0;
    std::uint64_t averageCollectionNanoseconds = 0;
    std::uint64_t p95CollectionNanoseconds = 0;
    std::uint64_t maximumCollectionNanoseconds = 0;
    std::size_t timingWindowSamples = 0;
    std::uint64_t lastSnapshotSprites = 0;
    std::uint64_t maximumSnapshotSprites = 0;
    std::uint64_t lastSnapshotCapacityBytes = 0;
    std::uint64_t maximumSnapshotCapacityBytes = 0;
    std::uint64_t lastUniqueTextureCount = 0;
    std::uint64_t maximumUniqueTextureCount = 0;
};

// Fixed-size timing history: recording a frame does not allocate memory.
// Averages cover the complete level; p95 values cover the latest 512 frames.
class RendererDiagnostics {
public:
    static constexpr std::size_t kTimingSampleWindow = 512;

    void reset() noexcept;
    void record(CaptureFrameSample const& sample) noexcept;
    RendererDiagnosticSummary summary() const noexcept;

private:
    std::uint64_t m_totalFrames = 0;
    std::uint64_t m_successfulFrames = 0;
    std::uint64_t m_failedFrames = 0;
    CaptureCounters m_totals{};
    std::uint64_t m_totalPostUpdateNanoseconds = 0;
    std::uint64_t m_totalCaptureNanoseconds = 0;
    std::uint64_t m_totalCollectionNanoseconds = 0;
    std::uint64_t m_maximumPostUpdateNanoseconds = 0;
    std::uint64_t m_maximumCaptureNanoseconds = 0;
    std::uint64_t m_maximumCollectionNanoseconds = 0;
    std::array<std::uint64_t, kTimingSampleWindow> m_postUpdateSamples{};
    std::array<std::uint64_t, kTimingSampleWindow> m_captureSamples{};
    std::array<std::uint64_t, kTimingSampleWindow> m_collectionSamples{};
    std::size_t m_timingSampleCount = 0;
    std::size_t m_nextTimingSample = 0;
    std::uint64_t m_lastSnapshotSprites = 0;
    std::uint64_t m_maximumSnapshotSprites = 0;
    std::uint64_t m_lastSnapshotCapacityBytes = 0;
    std::uint64_t m_maximumSnapshotCapacityBytes = 0;
    std::uint64_t m_lastUniqueTextureCount = 0;
    std::uint64_t m_maximumUniqueTextureCount = 0;
};

} // namespace dashcore::renderer::core
