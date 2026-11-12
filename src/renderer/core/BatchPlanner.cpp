#include "renderer/core/BatchPlanner.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace dashcore::renderer::core {
namespace {

BatchKey keyFor(SpriteDescriptor const& sprite) noexcept {
    return {
        sprite.order.zLayer,
        sprite.attributes.texture,
        sprite.attributes.blend
    };
}

} // namespace

std::vector<std::uint32_t> buildStableDrawOrder(
    std::span<SpriteDescriptor const> sprites
) {
    std::vector<std::uint32_t> order;
    if (sprites.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return order;
    }

    order.reserve(sprites.size());
    for (std::size_t index = 0; index < sprites.size(); ++index) {
        if (!validateSpriteDescriptor(sprites[index])) return {};
        order.push_back(static_cast<std::uint32_t>(index));
    }

    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t left, std::uint32_t right) {
        auto const& a = sprites[left].order;
        auto const& b = sprites[right].order;
        if (a.zLayer != b.zLayer) return a.zLayer < b.zLayer;
        return a.traversalOrder < b.traversalOrder;
    });
    return order;
}

std::vector<RenderBatch> planBatches(
    std::span<SpriteDescriptor const> sprites,
    std::span<std::uint32_t const> drawOrder
) {
    std::vector<RenderBatch> batches;
    if (drawOrder.empty()) return batches;

    if (drawOrder.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return batches;
    }

    std::set<std::uint32_t> seenIndices;
    for (std::size_t orderIndex = 0; orderIndex < drawOrder.size(); ++orderIndex) {
        auto const spriteIndex = drawOrder[orderIndex];
        if (spriteIndex >= sprites.size() ||
            !seenIndices.insert(spriteIndex).second ||
            !validateSpriteDescriptor(sprites[spriteIndex])) {
            return {};
        }

        auto const key = keyFor(sprites[spriteIndex]);
        if (batches.empty() || !(batches.back().key == key)) {
            RenderBatch batch;
            batch.key = key;
            batch.firstOrderIndex = static_cast<std::uint32_t>(orderIndex);
            batch.spriteIndices.push_back(spriteIndex);
            batches.push_back(std::move(batch));
        } else {
            batches.back().spriteIndices.push_back(spriteIndex);
        }
    }
    return batches;
}

} // namespace dashcore::renderer::core
