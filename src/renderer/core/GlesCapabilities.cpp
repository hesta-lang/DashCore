#include "renderer/core/GlesCapabilities.hpp"

#include <cstdio>
#include <cstring>

namespace dashcore::renderer::core {
namespace {

bool hasExtension(char const* extensions, char const* name) noexcept {
    if (!extensions || !name || *name == '\0') return false;
    auto const nameLength = std::strlen(name);
    auto const* current = extensions;

    while ((current = std::strstr(current, name)) != nullptr) {
        bool const leftBoundary = current == extensions || current[-1] == ' ';
        char const after = current[nameLength];
        bool const rightBoundary = after == '\0' || after == ' ';
        if (leftBoundary && rightBoundary) return true;
        current += nameLength;
    }
    return false;
}

bool atLeast(
    std::uint8_t major,
    std::uint8_t minor,
    std::uint8_t requiredMajor,
    std::uint8_t requiredMinor
) noexcept {
    return major > requiredMajor ||
        (major == requiredMajor && minor >= requiredMinor);
}

} // namespace

GlesCapabilities parseGlesCapabilities(
    char const* versionString,
    char const* extensionString,
    bool contextCurrent
) noexcept {
    GlesCapabilities result{};
    if (!contextCurrent || !versionString) return result;

    auto const* marker = std::strstr(versionString, "OpenGL ES");
    if (!marker) return result;

    marker += std::strlen("OpenGL ES");
    while (*marker == ' ' || *marker == '-') ++marker;

    unsigned int major = 0;
    unsigned int minor = 0;
    if (std::sscanf(marker, "%u.%u", &major, &minor) != 2 ||
        major > 255u || minor > 255u) {
        return result;
    }

    result.contextAvailable = true;
    result.majorVersion = static_cast<std::uint8_t>(major);
    result.minorVersion = static_cast<std::uint8_t>(minor);
    result.supportsBufferObjects = atLeast(result.majorVersion, result.minorVersion, 2, 0) ||
        hasExtension(extensionString, "GL_OES_vertex_buffer_object");
    result.supportsVertexArrayObjects = atLeast(result.majorVersion, result.minorVersion, 3, 0) ||
        hasExtension(extensionString, "GL_OES_vertex_array_object");
    result.supportsComputeShaders = atLeast(result.majorVersion, result.minorVersion, 3, 1) ||
        hasExtension(extensionString, "GL_EXT_compute_shader");
    result.supportsShaderStorageBuffers = atLeast(result.majorVersion, result.minorVersion, 3, 1) ||
        hasExtension(extensionString, "GL_EXT_shader_storage_buffer_object");
    return result;
}

} // namespace dashcore::renderer::core
