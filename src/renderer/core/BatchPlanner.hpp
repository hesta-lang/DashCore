#pragma once

#include "renderer/core/RendererTypes.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace dashcore::renderer::core {

struct BatchKey {
    std::int32_t zLayer = 0;
    TextureKey texture{};
    BlendState blend{};

    friend bool operator==(BatchKey const& left, BatchKey const& right) noexcept {
        return left.zLayer == right.zLayer &&
            left.texture.value == right.texture.value &&
            left.blend.sourceFactor == right.blend.sourceFactor &&
            left.blend.destinationFactor == right.blend.destinationFactor &&
            left.blend.supported == right.blend.supported;
    }
};

struct RenderBatch {
    BatchKey key{};
    std::uint32_t firstOrderIndex = 0;
    std::vector<std::uint32_t> spriteIndices;
};

std::vector<std::uint32_t> buildStableDrawOrder(
    std::span<SpriteDescriptor const> sprites
);

std::vector<RenderBatch> planBatches(
    std::span<SpriteDescriptor const> sprites,
    std::span<std::uint32_t const> drawOrder
);

} // namespace dashcore::renderer::core
