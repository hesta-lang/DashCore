#include "renderer/core/GlesCapabilities.hpp"

#include <cassert>
#include <string_view>

using namespace dashcore::renderer::core;

static GlesContextInfo computeContext(std::string_view extensions = {}) {
    return {
        "OpenGL ES 3.1 v1.r42p0",
        extensions,
        1u << 20u,
        128,
        65535,
    };
}

int main() {
    auto es2 = inspectGlesContext({"OpenGL ES 2.0 V@145.0", "", 0, 0, 0});
    assert(es2.recognizedOpenGlesContext);
    assert(es2.supportsVertexBuffers);
    assert(es2.supportsGlslEsSource);
    assert(!es2.supportsComputeShaders);
    assert(es2.preferredPath == GlesRenderPath::CpuVertexBuffers);

    auto es30 = inspectGlesContext({"OpenGL ES 3.0", "", 0, 0, 0});
    assert(es30.recognizedOpenGlesContext);
    assert(!es30.supportsComputeShaders);
    assert(!es30.supportsIndirectDraw);
    assert(es30.preferredPath == GlesRenderPath::CpuVertexBuffers);

    auto es31 = inspectGlesContext(computeContext());
    assert(es31.supportsComputeShaders);
    assert(es31.supportsShaderStorageBuffers);
    assert(es31.supportsIndirectDraw);
    assert(!es31.supportsMultiDrawIndirect);
    assert(es31.hasUsableComputeLimits);
    assert(es31.preferredPath == GlesRenderPath::ComputeWithIndirectDraw);

    auto multiDraw = inspectGlesContext(computeContext(
        "GL_EXT_multi_draw_indirect GL_EXT_disjoint_timer_query"
    ));
    assert(multiDraw.supportsMultiDrawIndirect);
    assert(multiDraw.supportsDisjointTimerQuery);
    assert(multiDraw.preferredPath == GlesRenderPath::ComputeWithMultiDrawIndirect);

    auto noLimits = inspectGlesContext({
        "OpenGL ES 3.2",
        "GL_EXT_multi_draw_indirect",
        0,
        128,
        65535,
    });
    assert(noLimits.supportsMultiDrawIndirect);
    assert(!noLimits.hasUsableComputeLimits);
    assert(noLimits.preferredPath == GlesRenderPath::CpuVertexBuffers);

    auto falsePositive = inspectGlesContext(computeContext(
        "GL_EXT_multi_draw_indirect_vendor_suffix"
    ));
    assert(!falsePositive.supportsMultiDrawIndirect);
    assert(falsePositive.preferredPath == GlesRenderPath::ComputeWithIndirectDraw);

    auto unknown = inspectGlesContext({"4.6 NVIDIA", "GL_EXT_multi_draw_indirect", 1, 1, 1});
    assert(!unknown.recognizedOpenGlesContext);
    assert(unknown.preferredPath == GlesRenderPath::VanillaFallback);

    auto es1 = inspectGlesContext({"OpenGL ES-CM 1.1", "", 0, 0, 0});
    assert(es1.recognizedOpenGlesContext);
    assert(!es1.supportsVertexBuffers);
    assert(es1.preferredPath == GlesRenderPath::VanillaFallback);

    assert(!es31.supportsSpirvSpecialization);
    assert(!es31.supportsOpenClInterop);
}
