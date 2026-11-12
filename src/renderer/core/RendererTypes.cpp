#include "renderer/core/RendererTypes.hpp"

#include <cmath>

namespace dashcore::renderer::core {
namespace {

bool isFinite(RectF const& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height);
}

bool hasPositiveArea(RectF const& rect) noexcept {
    return isFinite(rect) && rect.width > 0.f && rect.height > 0.f;
}

bool isNormalizedUv(RectF const& rect) noexcept {
    return hasPositiveArea(rect) && rect.x >= 0.f && rect.y >= 0.f &&
        rect.x + rect.width <= 1.f && rect.y + rect.height <= 1.f;
}

bool isFinite(Transform2D const& transform) noexcept {
    for (float value : transform.values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

} // namespace

bool validateSpriteDescriptor(SpriteDescriptor const& descriptor) noexcept {
    auto const& attributes = descriptor.attributes;
    if (!(descriptor.owner == descriptor.id.owner)) return false;
    if (!attributes.textureKnown || attributes.texture.value == 0) return false;
    if (!isNormalizedUv(attributes.uvRect)) return false;
    if (!hasPositiveArea(attributes.localRect)) return false;
    if (!isFinite(attributes.transform)) return false;
    if (!attributes.visible) return false;
    if (!attributes.blend.supported) return false;
    return true;
}

} // namespace dashcore::renderer::core
