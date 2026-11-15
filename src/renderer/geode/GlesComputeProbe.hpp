#pragma once

#include <cstdint>

namespace dashcore::renderer::geode {

enum class GlesComputeProbeStatus : std::uint8_t {
    NotRun,
    NoCurrentContext,
    UnsupportedVersion,
    MissingComputeLimits,
    ShaderCompileFailed,
    ProgramLinkFailed,
    BufferAllocationFailed,
    BufferMapFailed,
    OutputMismatch,
    Passed,
};

struct GlesComputeProbeResult {
    GlesComputeProbeStatus status = GlesComputeProbeStatus::NotRun;
    std::uint16_t majorVersion = 0;
    std::uint16_t minorVersion = 0;
    std::uint64_t maxShaderStorageBlockBytes = 0;
    bool dispatchExecuted = false;
    bool outputValidated = false;
};

GlesComputeProbeResult runGlesComputeProbe() noexcept;
char const* glesComputeProbeStatusName(GlesComputeProbeStatus status) noexcept;

} // namespace dashcore::renderer::geode
