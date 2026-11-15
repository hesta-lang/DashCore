#pragma once

#include <cstdint>

namespace dashcore::renderer::geode {

enum class GlesSsboAsyncTransferStatus : std::uint8_t {
    NotStarted,
    Running,
    NoCurrentContext,
    UnsupportedVersion,
    MissingComputeLimits,
    ShaderCompileFailed,
    ProgramLinkFailed,
    MissingUniform,
    BufferObjectCreationFailed,
    BufferAllocationFailed,
    FenceCreationFailed,
    FenceWaitFailed,
    FenceTimeout,
    BufferMapFailed,
    OutputMismatch,
    Cancelled,
    Passed,
};

struct GlesSsboAsyncTransferTiming {
    std::uint32_t payloadBytes = 0;
    std::uint32_t completedRepeats = 0;
    double allocationMs = 0.0;
    double uploadAverageMs = 0.0;
    double uploadMinimumMs = 0.0;
    double uploadMaximumMs = 0.0;
    double computeSubmitAverageMs = 0.0;
    double computeSubmitMinimumMs = 0.0;
    double computeSubmitMaximumMs = 0.0;
    double fenceLatencyAverageMs = 0.0;
    double fenceLatencyMinimumMs = 0.0;
    double fenceLatencyMaximumMs = 0.0;
    double readbackAverageMs = 0.0;
    double readbackMinimumMs = 0.0;
    double readbackMaximumMs = 0.0;
    double endToEndAverageMs = 0.0;
    double endToEndMinimumMs = 0.0;
    double endToEndMaximumMs = 0.0;
    double averageFencePollFrames = 0.0;
    std::uint32_t maximumFencePollFrames = 0;
};

struct GlesSsboAsyncTransferResult {
    GlesSsboAsyncTransferStatus status = GlesSsboAsyncTransferStatus::NotStarted;
    std::uint16_t majorVersion = 0;
    std::uint16_t minorVersion = 0;
    std::uint64_t maxShaderStorageBlockBytes = 0;
    std::uint64_t uploadBytesSubmitted = 0;
    std::uint64_t readbackBytesMapped = 0;
    std::uint32_t completedPayloadSizes = 0;
    std::uint32_t bufferStorageReallocations = 0;
    std::uint32_t dispatches = 0;
    std::uint32_t maxBufferBytes = 0;
    std::uint32_t sampleCount = 0;
    std::uint32_t fencePollCalls = 0;
    std::uint32_t pendingFencePolls = 0;
    std::uint32_t maximumFencePollFrames = 0;
    bool allPayloadSizesValidated = false;
    GlesSsboAsyncTransferTiming samples[4]{};
};

// Advances at most one bounded benchmark phase per call. Fence polling always
// uses a zero timeout; it never waits for GPU completion on the game thread.
GlesSsboAsyncTransferResult tickGlesSsboAsyncTransferBenchmark(
    bool enabled
) noexcept;

// Releases the private resources when a gameplay level exits.
void cancelGlesSsboAsyncTransferBenchmark() noexcept;

char const* glesSsboAsyncTransferStatusName(
    GlesSsboAsyncTransferStatus status
) noexcept;

} // namespace dashcore::renderer::geode
