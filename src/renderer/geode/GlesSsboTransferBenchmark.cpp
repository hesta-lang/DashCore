#include "renderer/geode/GlesSsboTransferBenchmark.hpp"

#if defined(GEODE_IS_ANDROID)

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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dashcore::renderer::geode {
namespace {

using Clock = std::chrono::steady_clock;

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

std::uint32_t makeInputValue(
    std::uint32_t sample,
    std::uint32_t repeat,
    std::uint32_t index
) noexcept {
    return (0x13579BDFu ^ ((sample + 1u) * 0x01020304u)) +
        (repeat * 0x1020304u) + (index * 0x01020304u);
}

std::uint32_t transformValue(std::uint32_t value, std::uint32_t index) noexcept {
    return (value * 1664525u + 1013904223u) ^ (index * 0x9E3779B9u);
}

double elapsedMs(Clock::time_point start, Clock::time_point end) noexcept {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct TimingAccumulator {
    double total = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = 0.0;
    std::uint32_t count = 0;

    void add(double value) noexcept {
        total += value;
        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;
        ++count;
    }

    double average() const noexcept {
        return count == 0 ? 0.0 : total / static_cast<double>(count);
    }

    double min() const noexcept {
        return count == 0 ? 0.0 : minimum;
    }
};

} // namespace

char const* glesSsboTransferBenchmarkStatusName(
    GlesSsboTransferBenchmarkStatus status
) noexcept {
    switch (status) {
        case GlesSsboTransferBenchmarkStatus::NotRun: return "not_run";
        case GlesSsboTransferBenchmarkStatus::NoCurrentContext: return "no_current_context";
        case GlesSsboTransferBenchmarkStatus::UnsupportedVersion: return "unsupported_version";
        case GlesSsboTransferBenchmarkStatus::MissingComputeLimits: return "missing_compute_limits";
        case GlesSsboTransferBenchmarkStatus::ShaderCompileFailed: return "shader_compile_failed";
        case GlesSsboTransferBenchmarkStatus::ProgramLinkFailed: return "program_link_failed";
        case GlesSsboTransferBenchmarkStatus::MissingUniform: return "missing_uniform";
        case GlesSsboTransferBenchmarkStatus::BufferObjectCreationFailed: return "buffer_object_creation_failed";
        case GlesSsboTransferBenchmarkStatus::BufferAllocationFailed: return "buffer_allocation_failed";
        case GlesSsboTransferBenchmarkStatus::BufferMapFailed: return "buffer_map_failed";
        case GlesSsboTransferBenchmarkStatus::OutputMismatch: return "output_mismatch";
        case GlesSsboTransferBenchmarkStatus::Passed: return "passed";
    }
    return "unknown";
}

GlesSsboTransferBenchmarkResult runGlesSsboTransferBenchmark() noexcept {
    GlesSsboTransferBenchmarkResult result{};

    auto const* version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
    if (!version) {
        result.status = GlesSsboTransferBenchmarkStatus::NoCurrentContext;
        return result;
    }
    if (!parseOpenGlesVersion(version, result.majorVersion, result.minorVersion) ||
        !supportsEs31(result.majorVersion, result.minorVersion)) {
        result.status = GlesSsboTransferBenchmarkStatus::UnsupportedVersion;
        return result;
    }

    // Four bounded payloads: 1, 4, 16, and 64 KiB per SSBO.
    static constexpr std::uint32_t payloadSizes[] = {
        256u, 1024u, 4096u, 16384u
    };
    static constexpr std::uint32_t repeatCount = 3u;
    static constexpr std::uint32_t maximumElements = 16384u;
    static constexpr std::uint32_t maximumBytes =
        maximumElements * static_cast<std::uint32_t>(sizeof(std::uint32_t));
    static constexpr std::uint32_t maximumWorkGroups = (maximumElements + 3u) / 4u;

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
        maxWorkGroupCountX < static_cast<GLint>(maximumWorkGroups) ||
        maxStorageBindings < 2) {
        result.status = GlesSsboTransferBenchmarkStatus::MissingComputeLimits;
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
        result.status = GlesSsboTransferBenchmarkStatus::ShaderCompileFailed;
        return result;
    }

    auto const* shaderText = reinterpret_cast<GLchar const*>(shaderSource);
    glShaderSource(shader, 1, &shaderText, nullptr);
    glCompileShader(shader);
    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE) {
        glDeleteShader(shader);
        result.status = GlesSsboTransferBenchmarkStatus::ShaderCompileFailed;
        return result;
    }

    auto const program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(shader);
        result.status = GlesSsboTransferBenchmarkStatus::ProgramLinkFailed;
        return result;
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        glDeleteProgram(program);
        result.status = GlesSsboTransferBenchmarkStatus::ProgramLinkFailed;
        return result;
    }

    auto const elementCountLocation = glGetUniformLocation(program, "uElementCount");
    if (elementCountLocation < 0) {
        glDeleteProgram(program);
        result.status = GlesSsboTransferBenchmarkStatus::MissingUniform;
        return result;
    }

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
        result.status = GlesSsboTransferBenchmarkStatus::BufferObjectCreationFailed;
        return result;
    }

    auto finish = [&](GlesSsboTransferBenchmarkStatus status) noexcept {
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
        result.allPayloadSizesValidated =
            status == GlesSsboTransferBenchmarkStatus::Passed;
        return result;
    };

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
    glUseProgram(program);

    std::uint32_t inputValues[maximumElements]{};
    std::uint32_t expectedValues[maximumElements]{};

    for (std::uint32_t sampleIndex = 0; sampleIndex < 4u; ++sampleIndex) {
        auto& sample = result.samples[sampleIndex];
        auto const elementCount = payloadSizes[sampleIndex];
        auto const byteCount = static_cast<GLsizeiptr>(
            elementCount * static_cast<std::uint32_t>(sizeof(std::uint32_t))
        );
        sample.payloadBytes = static_cast<std::uint32_t>(byteCount);
        result.sampleCount = sampleIndex + 1u;

        auto const allocationStart = Clock::now();
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_DRAW);
        ++result.bufferStorageReallocations;
        GLint inputAllocatedBytes = 0;
        glGetBufferParameteriv(
            GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &inputAllocatedBytes
        );
        if (inputAllocatedBytes < 0 ||
            static_cast<GLsizeiptr>(inputAllocatedBytes) < byteCount) {
            return finish(GlesSsboTransferBenchmarkStatus::BufferAllocationFailed);
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_READ);
        ++result.bufferStorageReallocations;
        GLint outputAllocatedBytes = 0;
        glGetBufferParameteriv(
            GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &outputAllocatedBytes
        );
        if (outputAllocatedBytes < 0 ||
            static_cast<GLsizeiptr>(outputAllocatedBytes) < byteCount) {
            return finish(GlesSsboTransferBenchmarkStatus::BufferAllocationFailed);
        }
        sample.allocationMs = elapsedMs(allocationStart, Clock::now());

        TimingAccumulator upload;
        TimingAccumulator computeSubmit;
        TimingAccumulator readback;
        TimingAccumulator cycle;

        for (std::uint32_t repeat = 0; repeat < repeatCount; ++repeat) {
            auto const seed = sampleIndex * repeatCount + repeat;
            for (std::uint32_t index = 0; index < elementCount; ++index) {
                inputValues[index] = makeInputValue(seed, repeat, index);
                expectedValues[index] = transformValue(inputValues[index], index);
            }

            auto const cycleStart = Clock::now();
            auto const uploadStart = Clock::now();
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
            glBufferSubData(
                GL_SHADER_STORAGE_BUFFER, 0, byteCount, inputValues
            );
            auto const uploadEnd = Clock::now();
            upload.add(elapsedMs(uploadStart, uploadEnd));
            result.uploadBytesSubmitted += static_cast<std::uint64_t>(byteCount);

            auto const computeStart = Clock::now();
            glUniform1ui(elementCountLocation, elementCount);
            auto const workGroupsX = (elementCount + 3u) / 4u;
            glDispatchCompute(workGroupsX, 1, 1);
            ++result.dispatches;
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
            auto const computeEnd = Clock::now();
            computeSubmit.add(elapsedMs(computeStart, computeEnd));

            auto const readbackStart = Clock::now();
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
            auto const* mapped = static_cast<std::uint32_t const*>(
                glMapBufferRange(
                    GL_SHADER_STORAGE_BUFFER, 0, byteCount, GL_MAP_READ_BIT
                )
            );
            if (!mapped) {
                return finish(GlesSsboTransferBenchmarkStatus::BufferMapFailed);
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
            auto const readbackEnd = Clock::now();
            readback.add(elapsedMs(readbackStart, readbackEnd));
            cycle.add(elapsedMs(cycleStart, readbackEnd));
            result.readbackBytesMapped += static_cast<std::uint64_t>(byteCount);

            if (!outputMatches || !unmapSucceeded) {
                return finish(GlesSsboTransferBenchmarkStatus::OutputMismatch);
            }
            ++sample.completedRepeats;
        }

        sample.uploadAverageMs = upload.average();
        sample.uploadMinimumMs = upload.min();
        sample.uploadMaximumMs = upload.maximum;
        sample.computeSubmitAverageMs = computeSubmit.average();
        sample.computeSubmitMinimumMs = computeSubmit.min();
        sample.computeSubmitMaximumMs = computeSubmit.maximum;
        sample.readbackAverageMs = readback.average();
        sample.readbackMinimumMs = readback.min();
        sample.readbackMaximumMs = readback.maximum;
        sample.cycleAverageMs = cycle.average();
        sample.cycleMinimumMs = cycle.min();
        sample.cycleMaximumMs = cycle.maximum;
        ++result.completedPayloadSizes;
    }

    return finish(GlesSsboTransferBenchmarkStatus::Passed);
}

} // namespace dashcore::renderer::geode

#else

namespace dashcore::renderer::geode {

char const* glesSsboTransferBenchmarkStatusName(
    GlesSsboTransferBenchmarkStatus status
) noexcept {
    return status == GlesSsboTransferBenchmarkStatus::NotRun
        ? "not_run"
        : "unsupported_platform";
}

GlesSsboTransferBenchmarkResult runGlesSsboTransferBenchmark() noexcept {
    GlesSsboTransferBenchmarkResult result{};
    result.status = GlesSsboTransferBenchmarkStatus::UnsupportedVersion;
    return result;
}

} // namespace dashcore::renderer::geode

#endif
