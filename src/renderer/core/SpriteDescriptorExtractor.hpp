#pragma once

#include "renderer/core/RendererTypes.hpp"

namespace dashcore::renderer::core {

bool extractSpriteDescriptor(
    RawSpriteAttributes const& attributes,
    ObjectId owner,
    std::uint32_t childIndex,
    LayerOrder order,
    SpriteDescriptor& output
) noexcept;

} // namespace dashcore::renderer::core
