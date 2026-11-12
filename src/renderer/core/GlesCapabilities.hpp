#pragma once

#include <cstdint>

namespace dashcore::renderer::core {

struct GlesCapabilities {
    bool contextAvailable = false;
    std::uint8_t majorVersion = 0;
    std::uint8_t minorVersion = 0;
    bool supportsBufferObjects = false;
    bool supportsVertexArrayObjects = false;
    bool supportsComputeShaders = false;
    bool supportsShaderStorageBuffers = false;
};

GlesCapabilities parseGlesCapabilities(
    char const* versionString,
    char const* extensionString,
    bool contextCurrent
) noexcept;

} // namespace dashcore::renderer::core
