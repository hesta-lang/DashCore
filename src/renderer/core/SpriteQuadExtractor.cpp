#include "renderer/core/SpriteQuadExtractor.hpp"

#include <cmath>

namespace dashcore::renderer::core {
namespace {

constexpr float kQuadTolerance = 0.001f;

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool near(float left, float right) noexcept {
    return std::fabs(left - right) <= kQuadTolerance;
}

bool sameColor(ColorRGBA8 const& left, ColorRGBA8 const& right) noexcept {
    return left.r == right.r && left.g == right.g &&
        left.b == right.b && left.a == right.a;
}

bool finiteVertex(SpriteQuadVertexData const& vertex) noexcept {
    return finite(vertex.x) && finite(vertex.y) && finite(vertex.z) &&
        finite(vertex.u) && finite(vertex.v);
}

bool normalized(float value) noexcept {
    return value >= 0.f && value <= 1.f;
}

} // namespace

bool extractAxisAlignedSpriteAttributes(
    SpriteQuadData const& quad,
    TextureKey texture,
    BlendState blend,
    bool visible,
    RawSpriteAttributes& output
) noexcept {
    output = {};

    auto const& bl = quad.bottomLeft;
    auto const& br = quad.bottomRight;
    auto const& tr = quad.topRight;
    auto const& tl = quad.topLeft;

    if (!visible || texture.value == 0 || !blend.supported) return false;
    if (!finiteVertex(bl) || !finiteVertex(br) ||
        !finiteVertex(tr) || !finiteVertex(tl)) {
        return false;
    }

    // GeometryBuilder does not model vertex depth yet.
    if (!near(bl.z, 0.f) || !near(br.z, 0.f) ||
        !near(tr.z, 0.f) || !near(tl.z, 0.f)) {
        return false;
    }

    // GeometryBuilder consumes only rectangles. Refuse skewed, rotated,
    // crossed, empty, or otherwise non-axis-aligned vertex layouts.
    if (!near(bl.y, br.y) || !near(br.x, tr.x) ||
        !near(tr.y, tl.y) || !near(tl.x, bl.x) ||
        !(br.x > bl.x) || !(tl.y > bl.y)) {
        return false;
    }

    // GeometryBuilder's atlas convention is BL=(uMin,vMax),
    // BR=(uMax,vMax), TR=(uMax,vMin), TL=(uMin,vMin).
    // Rotated/flipped atlas coordinates are intentionally not approximated.
    if (!near(bl.v, br.v) || !near(br.u, tr.u) ||
        !near(tr.v, tl.v) || !near(tl.u, bl.u) ||
        !(br.u > bl.u) || !(bl.v > tl.v)) {
        return false;
    }

    if (!normalized(bl.u) || !normalized(br.u) ||
        !normalized(tl.v) || !normalized(bl.v)) {
        return false;
    }

    if (!sameColor(bl.color, br.color) ||
        !sameColor(bl.color, tr.color) ||
        !sameColor(bl.color, tl.color)) {
        return false;
    }

    RawSpriteAttributes candidate{};
    candidate.texture = texture;
    candidate.textureKnown = true;
    candidate.localRect = {
        bl.x,
        bl.y,
        br.x - bl.x,
        tl.y - bl.y
    };
    candidate.uvRect = {
        bl.u,
        tl.v,
        br.u - bl.u,
        bl.v - tl.v
    };
    candidate.color = bl.color;

    // The quad's vertex alpha is already the effective sprite alpha in the
    // source Cocos quad. Do not multiply it by a second opacity value.
    candidate.opacity = 255;
    candidate.blend = blend;
    candidate.visible = true;

    SpriteDescriptor validation{};
    validation.owner = {1, 0};
    validation.id = {validation.owner, 0};
    validation.order = {0, 0};
    validation.attributes = candidate;
    if (!validateSpriteDescriptor(validation)) return false;

    output = candidate;
    return true;
}

} // namespace dashcore::renderer::core
