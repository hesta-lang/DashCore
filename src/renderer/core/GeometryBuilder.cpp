#include "renderer/core/GeometryBuilder.hpp"

#include <cmath>
#include <limits>
#include <set>

namespace dashcore::renderer::core {
namespace {

bool isFinite(float value) noexcept {
    return std::isfinite(value);
}

Vertex makeVertex(
    float x,
    float y,
    float u,
    float v,
    ColorRGBA8 color
) noexcept {
    return {x, y, u, v, color};
}

} // namespace

bool canRepresentSpriteCount(std::size_t spriteCount) noexcept {
    constexpr auto maxIndex = std::numeric_limits<std::uint32_t>::max();
    if (spriteCount > static_cast<std::size_t>(maxIndex / 4u)) return false;
    if (spriteCount > std::numeric_limits<std::size_t>::max() / 6u) return false;
    return true;
}

bool buildGeometry(
    RenderSnapshot const& snapshot,
    std::span<RenderBatch const> batches,
    GeometryData& output
) noexcept {
    output = {};
    std::size_t spriteCount = 0;
    for (auto const& batch : batches) {
        if (batch.spriteIndices.size() > std::numeric_limits<std::size_t>::max() - spriteCount) {
            return false;
        }
        spriteCount += batch.spriteIndices.size();
    }

    if (spriteCount == 0) return true;
    if (!canRepresentSpriteCount(spriteCount)) return false;

    try {
        GeometryData candidate;
        candidate.vertices.reserve(spriteCount * 4u);
        candidate.indices.reserve(spriteCount * 6u);

        std::set<std::uint32_t> seenSprites;
        for (auto const& batch : batches) {
            for (auto const spriteIndex : batch.spriteIndices) {
                if (spriteIndex >= snapshot.sprites.size() ||
                    !seenSprites.insert(spriteIndex).second) {
                    return false;
                }

                auto const& sprite = snapshot.sprites[spriteIndex];
                if (!validateSpriteDescriptor(sprite)) return false;

                auto const& attributes = sprite.attributes;
                BatchKey const expected{
                    sprite.order.zLayer,
                    attributes.texture,
                    attributes.blend
                };
                if (!(batch.key == expected)) return false;

                auto const& rect = attributes.localRect;
                auto const& uv = attributes.uvRect;
                auto const& matrix = attributes.transform.values;

                auto transformX = [&](float x, float y) {
                    return matrix[0] * x + matrix[2] * y + matrix[4];
                };
                auto transformY = [&](float x, float y) {
                    return matrix[1] * x + matrix[3] * y + matrix[5];
                };

                float const left = rect.x;
                float const right = rect.x + rect.width;
                float const bottom = rect.y;
                float const top = rect.y + rect.height;

                ColorRGBA8 color = attributes.color;
                color.a = static_cast<std::uint8_t>(
                    (static_cast<unsigned int>(color.a) * attributes.opacity + 127u) / 255u
                );

                Vertex vertices[4] = {
                    makeVertex(transformX(left, bottom), transformY(left, bottom), uv.x, uv.y + uv.height, color),
                    makeVertex(transformX(right, bottom), transformY(right, bottom), uv.x + uv.width, uv.y + uv.height, color),
                    makeVertex(transformX(right, top), transformY(right, top), uv.x + uv.width, uv.y, color),
                    makeVertex(transformX(left, top), transformY(left, top), uv.x, uv.y, color),
                };

                for (auto const& vertex : vertices) {
                    if (!isFinite(vertex.x) || !isFinite(vertex.y) ||
                        !isFinite(vertex.u) || !isFinite(vertex.v)) {
                        return false;
                    }
                }

                auto const base = static_cast<std::uint32_t>(candidate.vertices.size());
                candidate.vertices.insert(candidate.vertices.end(), std::begin(vertices), std::end(vertices));
                candidate.indices.insert(candidate.indices.end(), {
                    base, base + 1u, base + 2u,
                    base, base + 2u, base + 3u
                });
            }
        }

        output = std::move(candidate);
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

} // namespace dashcore::renderer::core
