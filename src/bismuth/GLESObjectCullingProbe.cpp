#include "GLESObjectCullingProbe.hpp"

#if defined(__ANDROID__)
// Geode/Cocos may map core vertex-array names to OES aliases.
#undef glBindVertexArray
#undef glDeleteVertexArrays
#undef glGenVertexArrays
#undef glBindVertexArrayOES
#undef glDeleteVertexArraysOES
#undef glGenVertexArraysOES

#include <GLES3/gl31.h>
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

using namespace geode::prelude;

namespace dashcore::bismuth {
namespace {
constexpr GLuint kInputBinding = 0;
constexpr GLuint kOutputBinding = 1;
constexpr std::uint32_t kForceVisible = 1u;

char const* kComputeSource = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct ObjectBounds {
    vec4 bounds;
    uint flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(std430, binding = 0) readonly buffer InputObjects {
    ObjectBounds objects[];
};
layout(std430, binding = 1) writeonly buffer VisibilityMask {
    uint visible[];
};

uniform uint uObjectCount;
uniform vec4 uViewport;
uniform vec2 uMargin;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= uObjectCount) return;

    ObjectBounds item = objects[index];
    bool forceVisible = (item.flags & 1u) != 0u;
    bool intersects =
        item.bounds.z >= uViewport.x - uMargin.x &&
        item.bounds.x <= uViewport.z + uMargin.x &&
        item.bounds.w >= uViewport.y - uMargin.y &&
        item.bounds.y <= uViewport.w + uMargin.y;

    visible[index] = (forceVisible || intersects) ? 1u : 0u;
}
)";

std::string shaderLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shader, length, &written, result.data());
    result.resize(static_cast<std::size_t>(written));
    return result;
}

std::string programLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(program, length, &written, result.data());
    result.resize(static_cast<std::size_t>(written));
    return result;
}
} // namespace

void runGLESObjectCullingProbeOnce(
    std::vector<GpuCullInput> const& objects,
    float viewportWidth,
    float viewportHeight
) {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;

    if (objects.empty() || viewportWidth <= 0.f || viewportHeight <= 0.f) {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: skipped; no usable scene objects or viewport.");
        return;
    }

    auto const* versionRaw = glGetString(GL_VERSION);
    auto const* rendererRaw = glGetString(GL_RENDERER);
    auto const version = versionRaw ? reinterpret_cast<char const*>(versionRaw) : "unknown";
    auto const renderer = rendererRaw ? reinterpret_cast<char const*>(rendererRaw) : "unknown";
    log::info("DashCore 0.8.2 BISMUTH-CULL: renderer='{}', context='{}', objects={}.",
        renderer, version, objects.size());

    GLint major = 0;
    GLint minor = 0;
    GLint maxBindings = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxBindings);
    if (major < 3 || (major == 3 && minor < 1) || maxBindings < 2) {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: unavailable; GLES 3.1 and two SSBO bindings are required.");
        return;
    }

    auto const started = std::chrono::steady_clock::now();
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (!shader) {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: glCreateShader failed.");
        return;
    }
    glShaderSource(shader, 1, &kComputeSource, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: shader compilation failed: {}", shaderLog(shader));
        glDeleteShader(shader);
        return;
    }

    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(shader);
        log::warn("DashCore 0.8.2 BISMUTH-CULL: glCreateProgram failed.");
        return;
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(shader);
    if (linked != GL_TRUE) {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: program link failed: {}", programLog(program));
        glDeleteProgram(program);
        return;
    }

    GLuint buffers[2]{};
    glGenBuffers(2, buffers);
    if (!buffers[0] || !buffers[1]) {
        if (buffers[0] || buffers[1]) glDeleteBuffers(2, buffers);
        glDeleteProgram(program);
        log::warn("DashCore 0.8.2 BISMUTH-CULL: SSBO allocation failed.");
        return;
    }

    GLint previousProgram = 0;
    GLint previousGenericBuffer = 0;
    GLint previousInputBinding = 0;
    GLint previousOutputBinding = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previousGenericBuffer);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, kInputBinding, &previousInputBinding);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, kOutputBinding, &previousOutputBinding);

    auto const inputBytes = static_cast<GLsizeiptr>(objects.size() * sizeof(GpuCullInput));
    auto const outputBytes = static_cast<GLsizeiptr>(objects.size() * sizeof(std::uint32_t));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, inputBytes, objects.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kInputBinding, buffers[0]);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, outputBytes, nullptr, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kOutputBinding, buffers[1]);

    glUseProgram(program);
    glUniform1ui(glGetUniformLocation(program, "uObjectCount"),
        static_cast<GLuint>(objects.size()));
    glUniform4f(glGetUniformLocation(program, "uViewport"),
        0.f, 0.f, viewportWidth, viewportHeight);
    glUniform2f(glGetUniformLocation(program, "uMargin"),
        viewportWidth * 0.75f, viewportHeight * 0.75f);

    GLuint const workGroups = static_cast<GLuint>((objects.size() + 63u) / 64u);
    auto const dispatchStart = std::chrono::steady_clock::now();
    glDispatchCompute(workGroups, 1, 1);
    auto const dispatchSubmitUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - dispatchStart).count();

    auto const readbackStart = std::chrono::steady_clock::now();
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
    auto const* mapped = static_cast<std::uint32_t const*>(
        glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, outputBytes, GL_MAP_READ_BIT));
    bool readbackSucceeded = mapped != nullptr;

    std::uint64_t cpuVisible = 0;
    std::uint64_t gpuVisible = 0;
    std::uint64_t mismatches = 0;
    if (readbackSucceeded) {
        for (std::size_t i = 0; i < objects.size(); ++i) {
            auto const& item = objects[i];
            bool const forceVisible = (item.flags & kForceVisible) != 0u;
            bool const intersects =
                item.maxX >= -viewportWidth * 0.75f &&
                item.minX <= viewportWidth * 1.75f &&
                item.maxY >= -viewportHeight * 0.75f &&
                item.minY <= viewportHeight * 1.75f;
            bool const expected = forceVisible || intersects;
            bool const actual = mapped[i] != 0u;
            if (expected) ++cpuVisible;
            if (actual) ++gpuVisible;
            if (expected != actual) ++mismatches;
        }
        readbackSucceeded = glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) == GL_TRUE;
    }

    auto const readbackUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - readbackStart).count();

    // Restore all GL state touched by the probe before returning to GD.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kInputBinding,
        static_cast<GLuint>(previousInputBinding));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kOutputBinding,
        static_cast<GLuint>(previousOutputBinding));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previousGenericBuffer));
    glUseProgram(static_cast<GLuint>(previousProgram));

    glDeleteBuffers(2, buffers);
    glDeleteProgram(program);

    auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();

    if (!readbackSucceeded) {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: SSBO readback failed; no game rendering was changed.");
        return;
    }

    log::info(
        "DashCore 0.8.2 BISMUTH-CULL: {} objects processed; CPU-visible={}, GPU-visible={}, mismatches={}, dispatchGroups={}, dispatchSubmit={}us, barrierReadbackCompare={}us, total={}us. DispatchSubmit is CPU submission time, not GPU execution time. Diagnostic only; live culling remains disabled.",
        objects.size(), cpuVisible, gpuVisible, mismatches, workGroups,
        dispatchSubmitUs, readbackUs, elapsedUs
    );
    if (mismatches == 0) {
        log::info("DashCore 0.8.2 BISMUTH-CULL: CPU/GPU parity PASS — compute output matches the CPU formula. This does not validate coordinate-space correctness or prove that off-screen objects were identified.");
    } else {
        log::warn("DashCore 0.8.2 BISMUTH-CULL: CPU/GPU parity FAIL — compute output differs from the CPU formula; live culling remains disabled.");
    }
}
} // namespace dashcore::bismuth

#else
namespace dashcore::bismuth {
void runGLESObjectCullingProbeOnce(
    std::vector<GpuCullInput> const&, float, float
) {}
}
#endif
