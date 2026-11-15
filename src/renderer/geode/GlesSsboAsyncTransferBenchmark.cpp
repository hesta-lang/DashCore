#include "renderer/geode/GlesSsboAsyncTransferBenchmark.hpp"

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

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dashcore::renderer::geode {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kRepeatCount = 3u;
constexpr std::uint32_t kPayloadSizes[] = {
    256u, 1024u, 4096u, 16384u
};
constexpr std::uint32_t kMaximumElements = 16384u;
constexpr std::uint32_t kMaximumBytes =
    kMaximumElements * static_cast<std::uint32_t>(sizeof(std::uint32_t));
constexpr std::uint32_t kMaximumWorkGroups = (kMaximumElements + 3u) / 4u;
constexpr std::uint32_t kMaximumFencePollFrames = 600u;

enum class Phase : std::uint8_t {
    Allocate,
    Submit,
    PollFence,
    Finished,
};

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

struct SampleAccumulators {
    TimingAccumulator upload;
    TimingAccumulator computeSubmit;
    TimingAccumulator fenceLatency;
    TimingAccumulator readback;
    TimingAccumulator endToEnd;
    std::uint64_t fencePollFramesTotal = 0;
    std::uint32_t maximumFencePollFrames = 0;
};

struct BenchmarkState {
    GlesSsboAsyncTransferResult result{};
    Phase phase = Phase::Allocate;
    GLuint program = 0;
    GLuint buffers[2]{};
    GLint elementCountLocation = -1;
    GLsync fence = nullptr;
    std::uint32_t sampleIndex = 0;
    std::uint32_t repeatIndex = 0;
    std::uint32_t currentFencePollFrames = 0;
    std::array<std::uint32_t, kMaximumElements> inputValues{};
    SampleAccumulators accumulators[4]{};
    Clock::time_point submittedAt{};
    Clock::time_point cycleStartedAt{};
};

BenchmarkState g_state;

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
        parsedMajor = parsedMajor * 10u +
            static_cast<unsigned>(*cursor - '0');
        ++cursor;
    }
    if (*cursor != '.') return false;
    ++cursor;
    if (*cursor < '0' || *cursor > '9') return false;

    unsigned parsedMinor = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsedMinor = parsedMinor * 10u +
            static_cast<unsigned>(*cursor - '0');
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

std::uint32_t transformValue(
    std::uint32_t value,
    std::uint32_t index
) noexcept {
    return (value * 1664525u + 1013904223u) ^
        (index * 0x9E3779B9u);
}

double elapsedMs(Clock::time_point start, Clock::time_point end) noexcept {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Each tick takes a fresh snapshot. No GL binding is intentionally left
// changed between gameplay frames.
class GlStateGuard {
public:
    GlStateGuard() noexcept {
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
        glGetIntegerv(
            GL_SHADER_STORAGE_BUFFER_BINDING,
            &m_genericStorageBuffer
        );
        glGetIntegeri_v(
            GL_SHADER_STORAGE_BUFFER_BINDING, 0, &m_inputStorageBuffer
        );
        glGetIntegeri_v(
            GL_SHADER_STORAGE_BUFFER_BINDING, 1, &m_outputStorageBuffer
        );
    }

    ~GlStateGuard() {
        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLuint>(m_inputStorageBuffer)
        );
        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER, 1,
            static_cast<GLuint>(m_outputStorageBuffer)
        );
        glBindBuffer(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<GLuint>(m_genericStorageBuffer)
        );
        glUseProgram(static_cast<GLuint>(m_program));
    }

    GlStateGuard(GlStateGuard const&) = delete;
    GlStateGuard& operator=(GlStateGuard const&) = delete;

private:
    GLint m_program = 0;
    GLint m_genericStorageBuffer = 0;
    GLint m_inputStorageBuffer = 0;
    GLint m_outputStorageBuffer = 0;
};

void releaseResources(BenchmarkState& state) noexcept {
    if (state.fence) {
        glDeleteSync(state.fence);
        state.fence = nullptr;
    }
    if (state.buffers[0] != 0 || state.buffers[1] != 0) {
        glDeleteBuffers(2, state.buffers);
        state.buffers[0] = 0;
        state.buffers[1] = 0;
    }
    if (state.program != 0) {
        glDeleteProgram(state.program);
        state.program = 0;
    }
}

void abandonResourcesAfterContextLoss(BenchmarkState& state) noexcept {
    // A lost context owns and destroys its GL objects. Do not try to delete
    // stale names through a different or absent context.
    state.fence = nullptr;
    state.buffers[0] = 0;
    state.buffers[1] = 0;
    state.program = 0;
}

GlesSsboAsyncTransferResult finish(
    BenchmarkState& state,
    GlesSsboAsyncTransferStatus status
) noexcept {
    releaseResources(state);
    state.phase = Phase::Finished;
    state.result.status = status;
    state.result.allPayloadSizesValidated =
        status == GlesSsboAsyncTransferStatus::Passed;
    return state.result;
}

GlesSsboAsyncTransferResult finishAfterContextLoss(
    BenchmarkState& state,
    GlesSsboAsyncTransferStatus status
) noexcept {
    abandonResourcesAfterContextLoss(state);
    state.phase = Phase::Finished;
    state.result.status = status;
    state.result.allPayloadSizesValidated = false;
    return state.result;
}

bool initialize(BenchmarkState& state) noexcept {
    auto const* version =
        reinterpret_cast<char const*>(glGetString(GL_VERSION));
    if (!version) {
        finishAfterContextLoss(
            state, GlesSsboAsyncTransferStatus::NoCurrentContext
        );
        return false;
    }

    if (!parseOpenGlesVersion(
            version, state.result.majorVersion, state.result.minorVersion
        ) ||
        !supportsEs31(
            state.result.majorVersion, state.result.minorVersion
        )) {
        finish(state, GlesSsboAsyncTransferStatus::UnsupportedVersion);
        return false;
    }

    GLint64 maxStorageBlock = 0;
    GLint maxWorkGroupInvocations = 0;
    GLint maxWorkGroupSizeX = 0;
    GLint maxWorkGroupCountX = 0;
    GLint maxStorageBindings = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxStorageBlock);
    glGetIntegerv(
        GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,
        &maxWorkGroupInvocations
    );
    glGetIntegeri_v(
        GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxWorkGroupSizeX
    );
    glGetIntegeri_v(
        GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCountX
    );
    glGetIntegerv(
        GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxStorageBindings
    );

    if (maxStorageBlock < static_cast<GLint64>(kMaximumBytes) ||
        maxWorkGroupInvocations < 4 ||
        maxWorkGroupSizeX < 4 ||
        maxWorkGroupCountX < static_cast<GLint>(kMaximumWorkGroups) ||
        maxStorageBindings < 2) {
        finish(state, GlesSsboAsyncTransferStatus::MissingComputeLimits);
        return false;
    }
    state.result.maxShaderStorageBlockBytes =
        static_cast<std::uint64_t>(maxStorageBlock);
    state.result.maxBufferBytes = kMaximumBytes;

    static constexpr char shaderSource[] =
        "#version 310 es\n"
        "layout(local_size_x = 4) in;\n"
        "layout(std430, binding = 0) readonly buffer DashCoreAsyncInput { uint inputValues[]; };\n"
        "layout(std430, binding = 1) writeonly buffer DashCoreAsyncOutput { uint outputValues[]; };\n"
        "uniform uint uElementCount;\n"
        "void main() {\n"
        "    uint index = gl_GlobalInvocationID.x;\n"
        "    if (index >= uElementCount) return;\n"
        "    uint value = inputValues[index];\n"
        "    outputValues[index] = (value * 1664525u + 1013904223u) ^ (index * 0x9E3779B9u);\n"
        "}\n";

    auto const shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0) {
        finish(state, GlesSsboAsyncTransferStatus::ShaderCompileFailed);
        return false;
    }

    auto const* shaderText =
        reinterpret_cast<GLchar const*>(shaderSource);
    glShaderSource(shader, 1, &shaderText, nullptr);
    glCompileShader(shader);
    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE) {
        glDeleteShader(shader);
        finish(state, GlesSsboAsyncTransferStatus::ShaderCompileFailed);
        return false;
    }

    state.program = glCreateProgram();
    if (state.program == 0) {
        glDeleteShader(shader);
        finish(state, GlesSsboAsyncTransferStatus::ProgramLinkFailed);
        return false;
    }
    glAttachShader(state.program, shader);
    glLinkProgram(state.program);
    glDeleteShader(shader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(state.program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        finish(state, GlesSsboAsyncTransferStatus::ProgramLinkFailed);
        return false;
    }

    state.elementCountLocation =
        glGetUniformLocation(state.program, "uElementCount");
    if (state.elementCountLocation < 0) {
        finish(state, GlesSsboAsyncTransferStatus::MissingUniform);
        return false;
    }

    glGenBuffers(2, state.buffers);
    if (state.buffers[0] == 0 || state.buffers[1] == 0) {
        finish(
            state, GlesSsboAsyncTransferStatus::BufferObjectCreationFailed
        );
        return false;
    }

    state.result.status = GlesSsboAsyncTransferStatus::Running;
    state.phase = Phase::Allocate;
    return true;
}

GlesSsboAsyncTransferResult allocateCurrentPayload(
    BenchmarkState& state
) noexcept {
    auto const elementCount = kPayloadSizes[state.sampleIndex];
    auto const byteCount = static_cast<GLsizeiptr>(
        elementCount * static_cast<std::uint32_t>(sizeof(std::uint32_t))
    );
    auto& sample = state.result.samples[state.sampleIndex];
    sample.payloadBytes = static_cast<std::uint32_t>(byteCount);
    state.result.sampleCount = state.sampleIndex + 1u;

    GlStateGuard guard;
    auto const allocationStart = Clock::now();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.buffers[0]);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_DRAW
    );
    ++state.result.bufferStorageReallocations;
    GLint inputAllocatedBytes = 0;
    glGetBufferParameteriv(
        GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &inputAllocatedBytes
    );
    if (inputAllocatedBytes < 0 ||
        static_cast<GLsizeiptr>(inputAllocatedBytes) < byteCount) {
        return finish(
            state, GlesSsboAsyncTransferStatus::BufferAllocationFailed
        );
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.buffers[1]);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER, byteCount, nullptr, GL_DYNAMIC_READ
    );
    ++state.result.bufferStorageReallocations;
    GLint outputAllocatedBytes = 0;
    glGetBufferParameteriv(
        GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &outputAllocatedBytes
    );
    if (outputAllocatedBytes < 0 ||
        static_cast<GLsizeiptr>(outputAllocatedBytes) < byteCount) {
        return finish(
            state, GlesSsboAsyncTransferStatus::BufferAllocationFailed
        );
    }

    sample.allocationMs = elapsedMs(allocationStart, Clock::now());
    state.phase = Phase::Submit;
    return state.result;
}

GlesSsboAsyncTransferResult submitOnePayload(
    BenchmarkState& state
) noexcept {
    auto const elementCount = kPayloadSizes[state.sampleIndex];
    auto const byteCount = static_cast<GLsizeiptr>(
        elementCount * static_cast<std::uint32_t>(sizeof(std::uint32_t))
    );
    auto const seed = state.sampleIndex * kRepeatCount + state.repeatIndex;
    auto& sample = state.result.samples[state.sampleIndex];
    auto& timings = state.accumulators[state.sampleIndex];

    for (std::uint32_t index = 0; index < elementCount; ++index) {
        state.inputValues[index] =
            makeInputValue(seed, state.repeatIndex, index);
    }

    GlStateGuard guard;
    state.cycleStartedAt = Clock::now();

    auto const uploadStart = Clock::now();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.buffers[0]);
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER, 0, byteCount, state.inputValues.data()
    );
    auto const uploadEnd = Clock::now();
    timings.upload.add(elapsedMs(uploadStart, uploadEnd));
    state.result.uploadBytesSubmitted +=
        static_cast<std::uint64_t>(byteCount);

    auto const computeStart = Clock::now();
    glUseProgram(state.program);
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER, 0, state.buffers[0]
    );
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER, 1, state.buffers[1]
    );
    glUniform1ui(state.elementCountLocation, elementCount);
    auto const workGroupsX = (elementCount + 3u) / 4u;
    glDispatchCompute(workGroupsX, 1, 1);
    ++state.result.dispatches;
    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
    );

    state.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!state.fence) {
        return finish(state, GlesSsboAsyncTransferStatus::FenceCreationFailed);
    }
    glFlush();
    auto const submitEnd = Clock::now();
    timings.computeSubmit.add(elapsedMs(computeStart, submitEnd));

    state.submittedAt = submitEnd;
    state.currentFencePollFrames = 0;
    state.phase = Phase::PollFence;
    return state.result;
}

GlesSsboAsyncTransferResult pollFenceAndValidate(
    BenchmarkState& state
) noexcept {
    ++state.result.fencePollCalls;
    ++state.currentFencePollFrames;

    // A zero timeout is essential: the gameplay thread only checks whether
    // the GPU has already completed this dispatch and never blocks for it.
    auto const waitStatus = glClientWaitSync(state.fence, 0, 0);
    if (waitStatus == GL_TIMEOUT_EXPIRED) {
        ++state.result.pendingFencePolls;
        if (state.currentFencePollFrames >= kMaximumFencePollFrames) {
            return finish(state, GlesSsboAsyncTransferStatus::FenceTimeout);
        }
        return state.result;
    }
    if (waitStatus != GL_ALREADY_SIGNALED &&
        waitStatus != GL_CONDITION_SATISFIED) {
        return finish(state, GlesSsboAsyncTransferStatus::FenceWaitFailed);
    }

    auto const fenceSignaledAt = Clock::now();
    auto& timings = state.accumulators[state.sampleIndex];
    timings.fenceLatency.add(
        elapsedMs(state.submittedAt, fenceSignaledAt)
    );
    timings.fencePollFramesTotal += state.currentFencePollFrames;
    if (state.currentFencePollFrames > timings.maximumFencePollFrames) {
        timings.maximumFencePollFrames = state.currentFencePollFrames;
    }
    if (state.currentFencePollFrames >
        state.result.maximumFencePollFrames) {
        state.result.maximumFencePollFrames =
            state.currentFencePollFrames;
    }

    glDeleteSync(state.fence);
    state.fence = nullptr;

    auto const elementCount = kPayloadSizes[state.sampleIndex];
    auto const byteCount = static_cast<GLsizeiptr>(
        elementCount * static_cast<std::uint32_t>(sizeof(std::uint32_t))
    );
    auto& sample = state.result.samples[state.sampleIndex];

    GlStateGuard guard;
    auto const readbackStart = Clock::now();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state.buffers[1]);
    auto const* mapped = static_cast<std::uint32_t const*>(
        glMapBufferRange(
            GL_SHADER_STORAGE_BUFFER, 0, byteCount, GL_MAP_READ_BIT
        )
    );
    if (!mapped) {
        return finish(state, GlesSsboAsyncTransferStatus::BufferMapFailed);
    }

    bool outputMatches = true;
    auto const seed = state.sampleIndex * kRepeatCount + state.repeatIndex;
    for (std::uint32_t index = 0; index < elementCount; ++index) {
        auto const expected = transformValue(
            makeInputValue(seed, state.repeatIndex, index), index
        );
        if (mapped[index] != expected) {
            outputMatches = false;
            break;
        }
    }
    auto const unmapSucceeded =
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) == GL_TRUE;
    auto const readbackEnd = Clock::now();

    timings.readback.add(elapsedMs(readbackStart, readbackEnd));
    timings.endToEnd.add(
        elapsedMs(state.cycleStartedAt, readbackEnd)
    );
    state.result.readbackBytesMapped +=
        static_cast<std::uint64_t>(byteCount);

    if (!outputMatches || !unmapSucceeded) {
        return finish(state, GlesSsboAsyncTransferStatus::OutputMismatch);
    }

    ++state.repeatIndex;
    sample.completedRepeats = state.repeatIndex;
    if (state.repeatIndex < kRepeatCount) {
        state.phase = Phase::Submit;
        return state.result;
    }

    sample.uploadAverageMs = timings.upload.average();
    sample.uploadMinimumMs = timings.upload.min();
    sample.uploadMaximumMs = timings.upload.maximum;
    sample.computeSubmitAverageMs = timings.computeSubmit.average();
    sample.computeSubmitMinimumMs = timings.computeSubmit.min();
    sample.computeSubmitMaximumMs = timings.computeSubmit.maximum;
    sample.fenceLatencyAverageMs = timings.fenceLatency.average();
    sample.fenceLatencyMinimumMs = timings.fenceLatency.min();
    sample.fenceLatencyMaximumMs = timings.fenceLatency.maximum;
    sample.readbackAverageMs = timings.readback.average();
    sample.readbackMinimumMs = timings.readback.min();
    sample.readbackMaximumMs = timings.readback.maximum;
    sample.endToEndAverageMs = timings.endToEnd.average();
    sample.endToEndMinimumMs = timings.endToEnd.min();
    sample.endToEndMaximumMs = timings.endToEnd.maximum;
    sample.averageFencePollFrames =
        timings.fencePollFramesTotal / static_cast<double>(kRepeatCount);
    sample.maximumFencePollFrames = timings.maximumFencePollFrames;

    ++state.result.completedPayloadSizes;
    ++state.sampleIndex;
    state.repeatIndex = 0;
    if (state.sampleIndex >= 4u) {
        return finish(state, GlesSsboAsyncTransferStatus::Passed);
    }

    state.phase = Phase::Allocate;
    return state.result;
}

} // namespace

char const* glesSsboAsyncTransferStatusName(
    GlesSsboAsyncTransferStatus status
) noexcept {
    switch (status) {
        case GlesSsboAsyncTransferStatus::NotStarted: return "not_started";
        case GlesSsboAsyncTransferStatus::Running: return "running";
        case GlesSsboAsyncTransferStatus::NoCurrentContext: return "no_current_context";
        case GlesSsboAsyncTransferStatus::UnsupportedVersion: return "unsupported_version";
        case GlesSsboAsyncTransferStatus::MissingComputeLimits: return "missing_compute_limits";
        case GlesSsboAsyncTransferStatus::ShaderCompileFailed: return "shader_compile_failed";
        case GlesSsboAsyncTransferStatus::ProgramLinkFailed: return "program_link_failed";
        case GlesSsboAsyncTransferStatus::MissingUniform: return "missing_uniform";
        case GlesSsboAsyncTransferStatus::BufferObjectCreationFailed: return "buffer_object_creation_failed";
        case GlesSsboAsyncTransferStatus::BufferAllocationFailed: return "buffer_allocation_failed";
        case GlesSsboAsyncTransferStatus::FenceCreationFailed: return "fence_creation_failed";
        case GlesSsboAsyncTransferStatus::FenceWaitFailed: return "fence_wait_failed";
        case GlesSsboAsyncTransferStatus::FenceTimeout: return "fence_timeout";
        case GlesSsboAsyncTransferStatus::BufferMapFailed: return "buffer_map_failed";
        case GlesSsboAsyncTransferStatus::OutputMismatch: return "output_mismatch";
        case GlesSsboAsyncTransferStatus::Cancelled: return "cancelled";
        case GlesSsboAsyncTransferStatus::Passed: return "passed";
    }
    return "unknown";
}

GlesSsboAsyncTransferResult tickGlesSsboAsyncTransferBenchmark(
    bool enabled
) noexcept {
    auto& state = g_state;
    if (!enabled) {
        if (state.result.status == GlesSsboAsyncTransferStatus::Running) {
            if (glGetString(GL_VERSION)) {
                return finish(state, GlesSsboAsyncTransferStatus::Cancelled);
            }
            return finishAfterContextLoss(
                state, GlesSsboAsyncTransferStatus::Cancelled
            );
        }
        return state.result;
    }

    if (state.result.status != GlesSsboAsyncTransferStatus::NotStarted &&
        state.result.status != GlesSsboAsyncTransferStatus::Running) {
        return state.result;
    }

    if (!glGetString(GL_VERSION)) {
        return finishAfterContextLoss(
            state, GlesSsboAsyncTransferStatus::NoCurrentContext
        );
    }

    if (state.result.status == GlesSsboAsyncTransferStatus::NotStarted) {
        initialize(state);
        return state.result;
    }

    switch (state.phase) {
        case Phase::Allocate:
            return allocateCurrentPayload(state);
        case Phase::Submit:
            return submitOnePayload(state);
        case Phase::PollFence:
            return pollFenceAndValidate(state);
        case Phase::Finished:
            return state.result;
    }
    return finish(state, GlesSsboAsyncTransferStatus::FenceWaitFailed);
}

void cancelGlesSsboAsyncTransferBenchmark() noexcept {
    auto& state = g_state;
    if (state.result.status != GlesSsboAsyncTransferStatus::Running) return;

    if (glGetString(GL_VERSION)) {
        finish(state, GlesSsboAsyncTransferStatus::Cancelled);
    } else {
        finishAfterContextLoss(
            state, GlesSsboAsyncTransferStatus::Cancelled
        );
    }
}

} // namespace dashcore::renderer::geode

#else

namespace dashcore::renderer::geode {

char const* glesSsboAsyncTransferStatusName(
    GlesSsboAsyncTransferStatus status
) noexcept {
    return status == GlesSsboAsyncTransferStatus::NotStarted
        ? "not_started"
        : "unsupported_platform";
}

GlesSsboAsyncTransferResult tickGlesSsboAsyncTransferBenchmark(
    bool enabled
) noexcept {
    GlesSsboAsyncTransferResult result{};
    if (enabled) {
        result.status = GlesSsboAsyncTransferStatus::UnsupportedVersion;
    }
    return result;
}

void cancelGlesSsboAsyncTransferBenchmark() noexcept {}

} // namespace dashcore::renderer::geode

#endif
