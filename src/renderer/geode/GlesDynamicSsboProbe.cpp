#include "renderer/geode/GlesDynamicSsboProbe.hpp"

#if defined(GEODE_IS_ANDROID)

// Keep Geode's Android CCGL compatibility aliases out of the GLES 3.1
// declarations in this translation unit only.
#ifdef glBindVertexArray
#undef glBindVertexArray
#endif
#ifdef glDeleteVertexArrays
#undef glDeleteVertexArrays
#endif
#ifdef glGenVertexArrays
#undef glGenVertexArrays
#endif
#ifdef glBindVertexArrayOES
#undef glBindVertexArrayOES
#endif
#ifdef glDeleteVertexArraysOES
#undef glDeleteVertexArraysOES
#endif
#ifdef glGenVertexArraysOES
#undef glGenVertexArraysOES
#endif

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

std::uint32_t makeInputValue(std::uint32_t pass, std::uint32_t index) noexcept {
    return (0x13579BDFu ^ ((pass + 1u) * 0x01020304u)) +
        (index * 0x01020304u);
}

std::uint32_t transformValue(std::uint32_t value, std::uint32_t index) noexcept {
    return (value * 1664525u + 1013904223u) ^ (index * 0x9E3779B9u);
}

} // namespace

char const* glesDynamicSsboProbeStatusName(
    GlesDynamicSsboProbeStatus status
) noexcept {
    switch (status) {
        case GlesDynamicSsboProbeStatus::NotRun: return "not_run";
        case GlesDynamicSsboProbeStatus::NoCurrentContext: return "no_current_context";
        case GlesDynamicSsboProbeStatus::UnsupportedVersion: return "unsupported_version";
        case GlesDynamicSsboProbeStatus::MissingComputeLimits: return "missing_compute_limits";
        case GlesDynamicSsboProbeStatus::ShaderCompileFailed: return "shader_compile_failed";
        case GlesDynamicSsboProbeStatus::ProgramLinkFailed: return "program_link_failed";
        case GlesDynamicSsboProbeStatus::MissingUniform: return "missing_uniform";
        case GlesDynamicSsboProbeStatus::BufferObjectCreationFailed: return "buffer_object_creation_failed";
        case GlesDynamicSsboProbeStatus::BufferAllocationFailed: return "buffer_allocation_failed";
        case GlesDynamicSsboProbeStatus::BufferMapFailed: return "buffer_map_failed";
        case GlesDynamicSsboProbeStatus::OutputMismatch: return "output_mismatch";
        case GlesDynamicSsboProbeStatus::Passed: return "passed";
    }
    return "unknown";
}

GlesDynamicSsboProbeResult runGlesDynamicSsboProbe() noexcept {
    GlesDynamicSsboProbeResult result{};

    auto const* version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
    if (!version) {
        result.status = GlesDynamicSsboProbeStatus::NoCurrentContext;
        return result;
    }
    if (!parseOpenGlesVersion(version, result.majorVersion, result.minorVersion) ||
        !supportsEs31(result.majorVersion, result.minorVersion)) {
        result.status = GlesDynamicSsboProbeStatus::UnsupportedVersion;
        return result;
    }

    // Maximum test payload is 256 uint32 values (1 KiB) per SSBO.
    static constexpr std::uint32_t batchSizes[] = {1u, 4u, 16u, 64u, 256u};
    static constexpr std::uint32_t maximumElements = 256u;
    static constexpr std::uint32_t maximumBytes =
        maximumElements * static_cast<std::uint32_t>(sizeof(std::uint32_t));

    GLint64 maxStorageBlock = 0;
    GLint maxWorkGroupInvocations = 0;
    GLint maxWorkGroupSizeX = 0;
    GLint maxWorkGroupCountX = 0;
    GLint maxStorageBindings = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxStorageBlock);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxWorkGroupInvocations);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxWorkGroupSizeX);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCountX);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxStorageBindings);

    if (maxStorageBlock < static_cast<GLint64>(maximumBytes) ||
        maxWorkGroupInvocations < 4 ||
        maxWorkGroupSizeX < 4 ||
        maxWorkGroupCountX < 64 ||
        maxStorageBindings < 2) {
        result.status = GlesDynamicSsboProbeStatus::MissingComputeLimits;
        return result;
    }
    result.maxShaderStorageBlockBytes = static_cast<std::uint64_t>(maxStorageBlock);
    result.maxBufferBytes = maximumBytes;

    static constexpr char shaderSource[] =
        "#version 310 es\n"
        "layout(local_size_x = 4) in;\n"
        "layout(std430, binding = 0) readonly buffer DashCoreInputBuffer { uint inputValues[]; };\n"
        "layout(std430, binding = 1) writeonly buffer DashCoreOutputBuffer { uint outputValues[]; };\n"
        "uniform uint uElementCount;\n"
        "void main() {\n"
        "    uint index = gl_GlobalInvocationID.x;\n"
        "    if (index >= uElementCount) return;\n"
        "    uint value = inputValues[index];\n"
        "    outputValues[index] = (value * 1664525u + 1013904223u) ^ (index * 0x9E3779B9u);\n"
        "}\n";

    auto const shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0) {
        result.status = GlesDynamicSsboProbeStatus::ShaderCompileFailed;
        return result;
    }

    auto const* shaderText = reinterpret_cast<GLchar const*>(shaderSource);
    glShaderSource(shader, 1, &shaderText, nullptr);
    glCompileShader(shader);
    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE) {
        glDeleteShader(shader);
        result.status = GlesDynamicSsboProbeStatus::ShaderCompileFailed;
        return result;
    }

    auto const program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(shader);
        result.status = GlesDynamicSsboProbeStatus::ProgramLinkFailed;
        return result;
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        glDeleteProgram(program);
        result.status = GlesDynamicSsboProbeStatus::ProgramLinkFailed;
        return result;
    }

    auto const elementCountLocation = glGetUniformLocation(program, "uElementCount");
    if (elementCountLocation < 0) {
        glDeleteProgram(program);
        result.status = GlesDynamicSsboProbeStatus::MissingUniform;
        return result;
    }

    // Save every GL binding this probe changes. No framebuffer, texture,
    // vertex-array, scene-node, or draw state is touched.
    GLint previousProgram = 0;
    GLint previousGenericStorageBuffer = 0;
    GLint previousInputStorageBuffer = 0;
    GLint previousOutputStorageBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previousGenericStorageBuffer);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &previousInputStorageBuffer);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 1, &previousOutputStorageBuffer);

    GLuint buffers[2]{};
    glGenBuffers(2, buffers);
    if (buffers[0] == 0 || buffers[1] == 0) {
        glDeleteBuffers(2, buffers);
        glDeleteProgram(program);
        result.status = GlesDynamicSsboProbeStatus::BufferObjectCreationFailed;
        return result;
    }

    auto finish = [&](GlesDynamicSsboProbeStatus status) noexcept {
        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLuint>(previousInputStorageBuffer)
        );
        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER, 1,
            static_cast<GLuint>(previousOutputStorageBuffer)
        );
        glBindBuffer(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<GLuint>(previousGenericStorageBuffer)
        );
        glUseProgram(static_cast<GLuint>(previousProgram));
        glDeleteBuffers(2, buffers);
        glDeleteProgram(program);
        result.status = status;
        result.allBatchesValidated = status == GlesDynamicSsboProbeStatus::Passed;
        return result;
    };

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
    glUseProgram(program);

    std::uint32_t inputValues[maximumElements]{};
    std::uint32_t expectedValues[maximumElements]{};

    for (std::uint32_t pass = 0; pass < 5u; ++pass) {
        auto const elementCount = batchSizes[pass];
        auto const byteCount = static_cast<GLsizeiptr>(
            elementCount * static_cast<std::uint32_t>(sizeof(std::uint32_t))
        );

        for (std::uint32_t index = 0; index < elementCount; ++index) {
            inputValues[index] = makeInputValue(pass, index);
            expectedValues[index] = transformValue(inputValues[index], index);
        }

        // Re-specify storage at each size, then upload fresh CPU-generated
        // input data into the same SSBO object.
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_DRAW);
        ++result.bufferStorageReallocations;
        GLint inputAllocatedBytes = 0;
        glGetBufferParameteriv(
            GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &inputAllocatedBytes
        );
        if (inputAllocatedBytes < byteCount) {
            return finish(GlesDynamicSsboProbeStatus::BufferAllocationFailed);
        }
        glBufferSubData(
            GL_SHADER_STORAGE_BUFFER, 0, byteCount, inputValues
        );
        result.uploadBytesSubmitted += static_cast<std::uint64_t>(byteCount);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_READ);
        ++result.bufferStorageReallocations;
        GLint outputAllocatedBytes = 0;
        glGetBufferParameteriv(
            GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &outputAllocatedBytes
        );
        if (outputAllocatedBytes < byteCount) {
            return finish(GlesDynamicSsboProbeStatus::BufferAllocationFailed);
        }

        glUniform1ui(elementCountLocation, elementCount);
        auto const workGroupsX = (elementCount + 3u) / 4u;
        glDispatchCompute(workGroupsX, 1, 1);
        ++result.dispatches;
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
        auto const* mapped = static_cast<std::uint32_t const*>(
            glMapBufferRange(
                GL_SHADER_STORAGE_BUFFER, 0, byteCount, GL_MAP_READ_BIT
            )
        );
        if (!mapped) {
            return finish(GlesDynamicSsboProbeStatus::BufferMapFailed);
        }

        bool outputMatches = true;
        for (std::uint32_t index = 0; index < elementCount; ++index) {
            if (mapped[index] != expectedValues[index]) {
                outputMatches = false;
                break;
            }
        }
        auto const unmapSucceeded =
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) == GL_TRUE;
        result.readbackBytesMapped += static_cast<std::uint64_t>(byteCount);

        if (!outputMatches || !unmapSucceeded) {
            return finish(GlesDynamicSsboProbeStatus::OutputMismatch);
        }

        ++result.completedBatches;
    }

    return finish(GlesDynamicSsboProbeStatus::Passed);
}

} // namespace dashcore::renderer::geode

#else

namespace dashcore::renderer::geode {

char const* glesDynamicSsboProbeStatusName(
    GlesDynamicSsboProbeStatus status
) noexcept {
    return status == GlesDynamicSsboProbeStatus::NotRun
        ? "not_run"
        : "unsupported_platform";
}

GlesDynamicSsboProbeResult runGlesDynamicSsboProbe() noexcept {
    GlesDynamicSsboProbeResult result{};
    result.status = GlesDynamicSsboProbeStatus::UnsupportedVersion;
    return result;
}

} // namespace dashcore::renderer::geode

#endif
