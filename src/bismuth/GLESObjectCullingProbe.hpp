#pragma once

#include <cstdint>
#include <vector>

namespace dashcore::bismuth {

// std430-compatible record: vec4 bounds + uint flags + 3 uint padding.
struct alignas(16) GpuCullInput {
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    std::uint32_t flags = 0;
    std::uint32_t padding[3]{};
};

static_assert(sizeof(GpuCullInput) == 32, "GpuCullInput must match std430 layout");

// Runs a one-shot, read-only GPU visibility-mask test over real scene objects.
// It never changes Geometry Dash visibility or draw calls.
void runGLESObjectCullingProbeOnce(
    std::vector<GpuCullInput> const& objects,
    float viewportWidth,
    float viewportHeight
);

} // namespace dashcore::bismuth
