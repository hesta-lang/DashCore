#pragma once

#include <cstdint>

namespace dashcore::renderer::geode {

enum class GlesSsboTransferBenchmarkStatus : std::uint8_t {
    NotRun,
    NoCurrentContext,
    UnsupportedVersion,
    MissingComputeLimits,
    ShaderCompileFailed,
    ProgramLinkFailed,
    MissingUniform,
    BufferObjectCreationFailed,
    BufferAllocationFailed,
    BufferMapFailed,
    OutputMismatch,
    Passed,
};

struct GlesSsboTransferTiming {
    std::uint32_t payloadBytes = 0;
    std::uint32_t completedRepeats = 0;
    double allocationMs = 0.0;
    double uploadAverageMs = 0.0;
    double uploadMinimumMs = 0.0;
    double uploadMaximumMs = 0.0;
    double computeSubmitAverageMs = 0.0;
    double computeSubmitMinimumMs = 0.0;
    double computeSubmitMaximumMs = 0.0;
    double readbackAverageMs = 0.0;
    double readbackMinimumMs = 0.0;
    double readbackMaximumMs = 0.0;
    double cycleAverageMs = 0.0;
    double cycleMinimumMs = 0.0;
    double cycleMaximumMs = 0.0;
};

struct GlesSsboTransferBenchmarkResult {
    GlesSsboTransferBenchmarkStatus status = GlesSsboTransferBenchmarkStatus::NotRun;
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
    bool allPayloadSizesValidated = false;
    GlesSsboTransferTiming samples[4]{};
};

GlesSsboTransferBenchmarkResult runGlesSsboTransferBenchmark() noexcept;
char const* glesSsboTransferBenchmarkStatusName(
    GlesSsboTransferBenchmarkStatus status
) noexcept;

} // namespace dashcore::renderer::geode
