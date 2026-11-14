#include "renderer/core/GlesCapabilities.hpp"

#include <limits>

namespace dashcore::renderer::core {
namespace {

bool parseUnsigned(
    std::string_view text,
    std::size_t& cursor,
    std::uint16_t& output
) noexcept {
    if (cursor >= text.size() || text[cursor] < '0' || text[cursor] > '9') {
        return false;
    }

    std::uint32_t value = 0;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        auto const digit = static_cast<std::uint32_t>(text[cursor] - '0');
        if (value > (std::numeric_limits<std::uint16_t>::max() - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++cursor;
    }

    output = static_cast<std::uint16_t>(value);
    return true;
}

bool parseVersion(
    std::string_view version,
    std::uint16_t& major,
    std::uint16_t& minor
) noexcept {
    constexpr std::string_view marker{"OpenGL ES"};
    auto const markerPosition = version.find(marker);
    if (markerPosition == std::string_view::npos) return false;

    std::size_t cursor = markerPosition + marker.size();
    while (cursor < version.size() &&
           (version[cursor] == ' ' || version[cursor] == '-' ||
            version[cursor] == 'C' || version[cursor] == 'M')) {
        ++cursor;
    }

    if (!parseUnsigned(version, cursor, major)) return false;
    if (cursor >= version.size() || version[cursor] != '.') return false;
    ++cursor;
    return parseUnsigned(version, cursor, minor);
}

bool hasExtension(std::string_view extensions, std::string_view wanted) noexcept {
    if (wanted.empty()) return false;

    std::size_t cursor = 0;
    while (cursor < extensions.size()) {
        while (cursor < extensions.size() &&
               (extensions[cursor] == ' ' || extensions[cursor] == '\n' ||
                extensions[cursor] == '\r' || extensions[cursor] == '\t')) {
            ++cursor;
        }

        auto const start = cursor;
        while (cursor < extensions.size() &&
               extensions[cursor] != ' ' && extensions[cursor] != '\n' &&
               extensions[cursor] != '\r' && extensions[cursor] != '\t') {
            ++cursor;
        }

        if (extensions.substr(start, cursor - start) == wanted) return true;
    }
    return false;
}

bool atLeast(
    std::uint16_t major,
    std::uint16_t minor,
    std::uint16_t requiredMajor,
    std::uint16_t requiredMinor
) noexcept {
    return major > requiredMajor ||
        (major == requiredMajor && minor >= requiredMinor);
}

} // namespace

GlesCapabilities inspectGlesContext(GlesContextInfo const& context) noexcept {
    GlesCapabilities result{};
    if (!parseVersion(context.versionString, result.majorVersion, result.minorVersion)) {
        return result;
    }

    result.recognizedOpenGlesContext = true;
    result.supportsVertexBuffers = atLeast(result.majorVersion, result.minorVersion, 2, 0);
    result.supportsGlslEsSource = result.supportsVertexBuffers;

    // OpenGL ES 3.1 makes compute shaders, shader-storage blocks and single
    // indirect draw commands part of the core API.
    bool const es31 = atLeast(result.majorVersion, result.minorVersion, 3, 1);
    result.supportsComputeShaders = es31;
    result.supportsShaderStorageBuffers = es31;
    result.supportsIndirectDraw = es31;

    // Multi-draw indirect is a separate ES extension, not an ES 3.1 core
    // feature. Exact extension-token matching avoids substring false positives.
    result.supportsMultiDrawIndirect = es31 &&
        hasExtension(context.extensionString, "GL_EXT_multi_draw_indirect");
    result.supportsDisjointTimerQuery =
        hasExtension(context.extensionString, "GL_EXT_disjoint_timer_query");

    result.hasUsableComputeLimits = es31 &&
        context.maxShaderStorageBlockSize > 0 &&
        context.maxComputeWorkGroupInvocations > 0 &&
        context.maxComputeWorkGroupCountX > 0;

    if (!result.supportsVertexBuffers) {
        result.preferredPath = GlesRenderPath::VanillaFallback;
    } else if (result.supportsComputeShaders &&
               result.supportsShaderStorageBuffers &&
               result.supportsIndirectDraw &&
               result.hasUsableComputeLimits) {
        result.preferredPath = result.supportsMultiDrawIndirect
            ? GlesRenderPath::ComputeWithMultiDrawIndirect
            : GlesRenderPath::ComputeWithIndirectDraw;
    } else {
        // This is a CPU-prepared VBO/IBO path. It never requires compute,
        // SSBOs, indirect commands, SPIR-V or OpenCL.
        result.preferredPath = GlesRenderPath::CpuVertexBuffers;
    }

    return result;
}

} // namespace dashcore::renderer::core
