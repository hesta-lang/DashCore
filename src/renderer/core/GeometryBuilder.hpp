#pragma once

#include "renderer/core/BatchPlanner.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dashcore::renderer::core {

struct Vertex {
    float x = 0.f;
    float y = 0.f;
    float u = 0.f;
    float v = 0.f;
    ColorRGBA8 color{};
};

struct GeometryData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

bool canRepresentSpriteCount(std::size_t spriteCount) noexcept;

bool buildGeometry(
    RenderSnapshot const& snapshot,
    std::span<RenderBatch const> batches,
    GeometryData& output
) noexcept;

} // namespace dashcore::renderer::core
