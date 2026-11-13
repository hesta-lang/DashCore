#pragma once

#include "renderer/core/RendererTypes.hpp"

namespace dashcore::renderer::core {

struct SpriteQuadVertexData {
    float x = 0.f;
    float y = 0.f;
    float u = 0.f;
    float v = 0.f;
    ColorRGBA8 color{};
};

struct SpriteQuadData {
    SpriteQuadVertexData bottomLeft{};
    SpriteQuadVertexData bottomRight{};
    SpriteQuadVertexData topRight{};
    SpriteQuadVertexData topLeft{};
};

// Extract only axis-aligned, unrotated quads with a normalized rectangular UV
// mapping and uniform vertex color. Unsupported forms are rejected rather
// than approximated.
bool extractAxisAlignedSpriteAttributes(
    SpriteQuadData const& quad,
    TextureKey texture,
    BlendState blend,
    bool visible,
    RawSpriteAttributes& output
) noexcept;

} // namespace dashcore::renderer::core
