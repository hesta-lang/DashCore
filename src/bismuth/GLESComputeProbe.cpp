#include "GLESComputeProbe.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl31.h>
#include <cstdint>
#include <string>
#include <vector>
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace dashcore::bismuth {
namespace {
constexpr GLuint kProbeBinding = 0;
constexpr GLuint kExpected = 0xB15B17u;

char const* kComputeSource = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(std430, binding = 0) buffer ProbeOutput {
    uint resultValue;
};
void main() {
    // Minimal GLES 3.1 compute operation used to validate the execution path.
    resultValue = 0x00B15B17u;
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

void runGLESComputeProbeOnce() {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;

    auto const* versionRaw = glGetString(GL_VERSION);
    auto const* rendererRaw = glGetString(GL_RENDERER);
    auto const version = versionRaw
        ? reinterpret_cast<char const*>(versionRaw) : "unknown";
    auto const renderer = rendererRaw
        ? reinterpret_cast<char const*>(rendererRaw) : "unknown";

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    log::info("DashCore 0.6.3 BISMUTH-GLES: context='{}', renderer='{}'.",
        version, renderer);

    if (major < 3 || (major == 3 && minor < 1)) {
        log::warn("DashCore 0.6.3 BISMUTH-GLES: compute backend unavailable; GLES 3.1 is required. Original GD renderer remains active.");
        return;
    }

    GLint maxBindings = 0;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxBindings);
    if (maxBindings <= static_cast<GLint>(kProbeBinding)) {
        log::warn("DashCore 0.6.3 BISMUTH-GLES: no usable SSBO binding; original GD renderer remains active.");
        return;
    }

    GLint previousProgram = 0;
    GLint previousGenericBuffer = 0;
    GLint previousIndexedBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previousGenericBuffer);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, kProbeBinding,
        &previousIndexedBuffer);

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (!shader) {
        log::warn("DashCore 0.6.3 BISMUTH-GLES: glCreateShader failed.");
        return;
    }

    glShaderSource(shader, 1, &kComputeSource, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        auto const details = shaderLog(shader);
        log::warn("DashCore 0.6.3 BISMUTH-GLES: compute shader compilation failed: {}", details);
        glDeleteShader(shader);
        return;
    }

    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(shader);
        log::warn("DashCore 0.6.3 BISMUTH-GLES: glCreateProgram failed.");
        return;
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(shader);

    if (linked != GL_TRUE) {
        auto const details = programLog(program);
        log::warn("DashCore 0.6.3 BISMUTH-GLES: compute program link failed: {}", details);
        glDeleteProgram(program);
        return;
    }

    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    if (!buffer) {
        glDeleteProgram(program);
        log::warn("DashCore 0.6.3 BISMUTH-GLES: SSBO allocation failed.");
        return;
    }

    std::uint32_t initialValue = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(initialValue), &initialValue,
        GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kProbeBinding, buffer);

    glUseProgram(program);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    auto const* mapped = static_cast<std::uint32_t const*>(
        glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(initialValue),
            GL_MAP_READ_BIT));
    bool success = false;
    if (mapped) {
        success = *mapped == kExpected;
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }

    // Restore the state touched by this one-shot probe before returning to GD.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kProbeBinding,
        static_cast<GLuint>(previousIndexedBuffer));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLuint>(previousGenericBuffer));
    glUseProgram(static_cast<GLuint>(previousProgram));

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);

    if (success) {
        log::info("DashCore 0.6.3 BISMUTH-GLES: PASS — GLES compute shader compiled, linked, dispatched, and wrote the expected SSBO value. No game objects or draw calls were modified.");
    } else {
        log::warn("DashCore 0.6.3 BISMUTH-GLES: FAIL — compute dispatch/SSBO readback did not match. Original GD renderer remains active.");
    }
}
} // namespace dashcore::bismuth

#else
namespace dashcore::bismuth {
void runGLESComputeProbeOnce() {}
}
#endif
