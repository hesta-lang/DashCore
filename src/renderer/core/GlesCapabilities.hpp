#pragma once

#include <cstdint>
#include <string_view>

namespace dashcore::renderer::core {

enum class GlesRenderPath : std::uint8_t {
    VanillaFallback,
    CpuVertexBuffers,
    ComputeWithIndirectDraw,
    ComputeWithMultiDrawIndirect,
};

// Values supplied by a context probe running on the active GL render thread.
// This model deliberately performs no GL/EGL calls and owns no context state.
struct GlesContextInfo {
    std::string_view versionString{};
    std::string_view extensionString{};
    std::uint32_t maxShaderStorageBlockSize = 0;
    std::uint32_t maxComputeWorkGroupInvocations = 0;
    std::uint32_t maxComputeWorkGroupCountX = 0;
};

struct GlesCapabilities {
    std::uint16_t majorVersion = 0;
    std::uint16_t minorVersion = 0;

    bool recognizedOpenGlesContext = false;
    bool supportsVertexBuffers = false;
    bool supportsGlslEsSource = false;
    bool supportsComputeShaders = false;
    bool supportsShaderStorageBuffers = false;
    bool supportsIndirectDraw = false;
    bool supportsMultiDrawIndirect = false;
    bool supportsDisjointTimerQuery = false;
    bool hasUsableComputeLimits = false;

    // These remain false by design: this Android path does not depend on
    // desktop OpenGL SPIR-V specialization or OpenCL/GL interop.
    bool supportsSpirvSpecialization = false;
    bool supportsOpenClInterop = false;

    GlesRenderPath preferredPath = GlesRenderPath::VanillaFallback;
};

GlesCapabilities inspectGlesContext(GlesContextInfo const& context) noexcept;

} // namespace dashcore::renderer::core
