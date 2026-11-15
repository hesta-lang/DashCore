#pragma once

#include <cstdint>

namespace dashcore::renderer::geode {

enum class GlesDynamicSsboProbeStatus : std::uint8_t {
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

struct GlesDynamicSsboProbeResult {
    GlesDynamicSsboProbeStatus status = GlesDynamicSsboProbeStatus::NotRun;
    std::uint16_t majorVersion = 0;
    std::uint16_t minorVersion = 0;
    std::uint64_t maxShaderStorageBlockBytes = 0;
    std::uint64_t uploadBytesSubmitted = 0;
    std::uint64_t readbackBytesMapped = 0;
    std::uint32_t completedBatches = 0;
    std::uint32_t bufferStorageReallocations = 0;
    std::uint32_t dispatches = 0;
    std::uint32_t maxBufferBytes = 0;
    bool allBatchesValidated = false;
};

GlesDynamicSsboProbeResult runGlesDynamicSsboProbe() noexcept;
char const* glesDynamicSsboProbeStatusName(GlesDynamicSsboProbeStatus status) noexcept;

} // namespace dashcore::renderer::geode
