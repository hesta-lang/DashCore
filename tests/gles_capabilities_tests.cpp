#include "renderer/core/GlesCapabilities.hpp"

#include <cassert>

using dashcore::renderer::core::parseGlesCapabilities;

int main() {
    auto const noContext = parseGlesCapabilities(nullptr, nullptr, false);
    assert(!noContext.contextAvailable);
    assert(!noContext.extensionEnumerationAvailable);

    auto const maliProfile = parseGlesCapabilities(
        "OpenGL ES 3.2 V@xxxx",
        "GL_EXT_disjoint_timer_query",
        true
    );
    assert(maliProfile.contextAvailable);
    assert(maliProfile.extensionEnumerationAvailable);
    assert(maliProfile.majorVersion == 3);
    assert(maliProfile.minorVersion == 2);
    assert(maliProfile.supportsBufferObjects);
    assert(maliProfile.supportsVertexArrayObjects);
    assert(maliProfile.supportsComputeShaders);
    assert(maliProfile.supportsShaderStorageBuffers);
    assert(maliProfile.supportsDrawIndirect);
    assert(!maliProfile.supportsMultiDrawIndirect);
    assert(!maliProfile.supportsShaderDrawParameters);
    assert(!maliProfile.supportsSpirVShaders);
    assert(maliProfile.supportsDisjointTimerQuery);

    auto const extendedProfile = parseGlesCapabilities(
        "OpenGL ES 3.2",
        "GL_EXT_multi_draw_indirect GL_EXT_shader_draw_parameters "
        "GL_ARB_gl_spirv GL_EXT_disjoint_timer_query",
        true
    );
    assert(extendedProfile.supportsMultiDrawIndirect);
    assert(extendedProfile.supportsShaderDrawParameters);
    assert(extendedProfile.supportsSpirVShaders);
    assert(extendedProfile.supportsDisjointTimerQuery);

    auto const falseSubstringMatch = parseGlesCapabilities(
        "OpenGL ES 3.2",
        "GL_EXT_multi_draw_indirect_fake GL_EXT_disjoint_timer_query_suffix",
        true
    );
    assert(!falseSubstringMatch.supportsMultiDrawIndirect);
    assert(!falseSubstringMatch.supportsDisjointTimerQuery);

    auto const es30 = parseGlesCapabilities(
        "OpenGL ES 3.0",
        "GL_EXT_compute_shader GL_EXT_shader_storage_buffer_object "
        "GL_EXT_draw_indirect",
        true
    );
    assert(es30.supportsDrawIndirect);
    assert(es30.supportsComputeShaders);
    assert(es30.supportsShaderStorageBuffers);
}
