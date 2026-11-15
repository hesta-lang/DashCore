#include "renderer/geode/GlesComputeProbe.hpp"

#if defined(GEODE_IS_ANDROID)

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GLES3/gl31.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dashcore::renderer::geode {
namespace {

bool parseOpenGlesVersion(
    char const* version,
    std::uint16_t& major,
    std::uint16_t& minor
) noexcept {
    if (!version) return false;
    auto const* marker = std::strstr(version, "OpenGL ES ");
    if (!marker) return false;

    auto const* cursor = marker + 10;
    if (*cursor < '0' || *cursor > '9') return false;

    unsigned parsedMajor = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsedMajor = parsedMajor * 10u + static_cast<unsigned>(*cursor - '0');
        ++cursor;
    }
    if (*cursor != '.') return false;
    ++cursor;
    if (*cursor < '0' || *cursor > '9') return false;

    unsigned parsedMinor = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsedMinor = parsedMinor * 10u + static_cast<unsigned>(*cursor - '0');
        ++cursor;
    }
    if (parsedMajor > UINT16_MAX || parsedMinor > UINT16_MAX) return false;
    major = static_cast<std::uint16_t>(parsedMajor);
    minor = static_cast<std::uint16_t>(parsedMinor);
    return true;
}

bool supportsEs31(std::uint16_t major, std::uint16_t minor) noexcept {
    return major > 3 || (major == 3 && minor >= 1);
}

} // namespace

char const* glesComputeProbeStatusName(GlesComputeProbeStatus status) noexcept {
    switch (status) {
        case GlesComputeProbeStatus::NotRun: return "not_run";
        case GlesComputeProbeStatus::NoCurrentContext: return "no_current_context";
        case GlesComputeProbeStatus::UnsupportedVersion: return "unsupported_version";
        case GlesComputeProbeStatus::MissingComputeLimits: return "missing_compute_limits";
        case GlesComputeProbeStatus::ShaderCompileFailed: return "shader_compile_failed";
        case GlesComputeProbeStatus::ProgramLinkFailed: return "program_link_failed";
        case GlesComputeProbeStatus::BufferAllocationFailed: return "buffer_allocation_failed";
        case GlesComputeProbeStatus::BufferMapFailed: return "buffer_map_failed";
        case GlesComputeProbeStatus::OutputMismatch: return "output_mismatch";
        case GlesComputeProbeStatus::Passed: return "passed";
    }
    return "unknown";
}

GlesComputeProbeResult runGlesComputeProbe() noexcept {
    GlesComputeProbeResult result{};
    auto const* version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
    if (!version) {
        result.status = GlesComputeProbeStatus::NoCurrentContext;
        return result;
    }
    if (!parseOpenGlesVersion(version, result.majorVersion, result.minorVersion) ||
        !supportsEs31(result.majorVersion, result.minorVersion)) {
        result.status = GlesComputeProbeStatus::UnsupportedVersion;
        return result;
    }

    GLint64 maxStorageBlock = 0;
    GLint maxWorkGroupInvocations = 0;
    GLint maxWorkGroupCountX = 0;
    GLint maxStorageBindings = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxStorageBlock);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxWorkGroupInvocations);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCountX);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxStorageBindings);

    if (maxStorageBlock < 16 || maxWorkGroupInvocations < 4 ||
        maxWorkGroupCountX < 1 || maxStorageBindings < 1) {
        result.status = GlesComputeProbeStatus::MissingComputeLimits;
        return result;
    }
    result.maxShaderStorageBlockBytes = static_cast<std::uint64_t>(maxStorageBlock);

    static constexpr char shaderSource[] =
        "#version 310 es\n"
        "layout(local_size_x = 4) in;\n"
        "layout(std430, binding = 0) buffer DashCoreProbeBuffer { uint resultValues[]; };\n"
        "void main() {\n"
        "    uint index = gl_GlobalInvocationID.x;\n"
        "    resultValues[index] = 0xD45C0000u + index;\n"
        "}\n";

    auto const shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0) {
        result.status = GlesComputeProbeStatus::ShaderCompileFailed;
        return result;
    }

    auto const* shaderText = reinterpret_cast<GLchar const*>(shaderSource);
    glShaderSource(shader, 1, &shaderText, nullptr);
    glCompileShader(shader);
    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE) {
        glDeleteShader(shader);
        result.status = GlesComputeProbeStatus::ShaderCompileFailed;
        return result;
    }

    auto const program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(shader);
        result.status = GlesComputeProbeStatus::ProgramLinkFailed;
        return result;
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        glDeleteProgram(program);
        result.status = GlesComputeProbeStatus::ProgramLinkFailed;
        return result;
    }

    // Preserve the existing program and SSBO bindings. The test never touches
    // vertex arrays, textures, framebuffers, Cocos draw commands, or scene nodes.
    GLint previousProgram = 0;
    GLint previousGenericStorageBuffer = 0;
    GLint previousIndexedStorageBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previousGenericStorageBuffer);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &previousIndexedStorageBuffer);

    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    if (buffer == 0) {
        glUseProgram(static_cast<GLuint>(previousProgram));
        glDeleteProgram(program);
        result.status = GlesComputeProbeStatus::BufferAllocationFailed;
        return result;
    }

    std::uint32_t expected[4] = {
        0xD45C0000u, 0xD45C0001u, 0xD45C0002u, 0xD45C0003u
    };
    bool outputMatches = false;
    bool unmapSucceeded = false;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(sizeof(expected)),
        nullptr,
        GL_DYNAMIC_READ
    );
    GLint allocatedBytes = 0;
    glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &allocatedBytes);
    if (allocatedBytes < static_cast<GLint>(sizeof(expected))) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previousGenericStorageBuffer));
        glDeleteBuffers(1, &buffer);
        glDeleteProgram(program);
        result.status = GlesComputeProbeStatus::BufferAllocationFailed;
        return result;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
    glUseProgram(program);
    glDispatchCompute(1, 1, 1);
    result.dispatchExecuted = true;
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    auto* mapped = static_cast<std::uint32_t*>(
        glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(expected), GL_MAP_READ_BIT)
    );
    if (mapped) {
        outputMatches = true;
        for (std::size_t i = 0; i < 4; ++i) {
            if (mapped[i] != expected[i]) outputMatches = false;
        }
        unmapSucceeded = glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) == GL_TRUE;
    }

    // Restore the indexed binding first, then the generic target binding.
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLuint>(previousIndexedStorageBuffer)
    );
    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previousGenericStorageBuffer)
    );
    glUseProgram(static_cast<GLuint>(previousProgram));

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);

    if (!mapped) {
        result.status = GlesComputeProbeStatus::BufferMapFailed;
    } else if (!outputMatches || !unmapSucceeded) {
        result.status = GlesComputeProbeStatus::OutputMismatch;
    } else {
        result.status = GlesComputeProbeStatus::Passed;
        result.outputValidated = true;
    }
    return result;
}

} // namespace dashcore::renderer::geode

#else

namespace dashcore::renderer::geode {

char const* glesComputeProbeStatusName(GlesComputeProbeStatus status) noexcept {
    return status == GlesComputeProbeStatus::NotRun ? "not_run" : "unsupported_platform";
}

GlesComputeProbeResult runGlesComputeProbe() noexcept {
    GlesComputeProbeResult result{};
    result.status = GlesComputeProbeStatus::UnsupportedVersion;
    return result;
}

} // namespace dashcore::renderer::geode

#endif
