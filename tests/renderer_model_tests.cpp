#include "renderer/core/RendererTypes.hpp"

#include <cassert>
#include <limits>

using namespace dashcore::renderer::core;

static SpriteDescriptor makeValid() {
    SpriteDescriptor value{};
    value.owner = {1, 7};
    value.id = {value.owner, 2};
    value.order = {0, 10};
    value.attributes.texture = {42};
    value.attributes.textureKnown = true;
    value.attributes.uvRect = {0.f, 0.f, 0.25f, 0.25f};
    value.attributes.localRect = {-8.f, -8.f, 16.f, 16.f};
    value.attributes.blend = {1, 771, true};
    return value;
}

int main() {
    auto valid = makeValid();
    assert(validateSpriteDescriptor(valid));

    auto nanTransform = valid;
    nanTransform.attributes.transform.values[2] = std::numeric_limits<float>::quiet_NaN();
    assert(!validateSpriteDescriptor(nanTransform));

    auto infiniteTransform = valid;
    infiniteTransform.attributes.transform.values[0] = std::numeric_limits<float>::infinity();
    assert(!validateSpriteDescriptor(infiniteTransform));

    auto invalidLocalRect = valid;
    invalidLocalRect.attributes.localRect.width = 0.f;
    assert(!validateSpriteDescriptor(invalidLocalRect));

    auto invalidUv = valid;
    invalidUv.attributes.uvRect = {0.9f, 0.f, 0.2f, 0.2f};
    assert(!validateSpriteDescriptor(invalidUv));

    auto unknownTexture = valid;
    unknownTexture.attributes.textureKnown = false;
    assert(!validateSpriteDescriptor(unknownTexture));

    auto unsupportedBlend = valid;
    unsupportedBlend.attributes.blend.supported = false;
    assert(!validateSpriteDescriptor(unsupportedBlend));

    auto hiddenSprite = valid;
    hiddenSprite.attributes.visible = false;
    assert(!validateSpriteDescriptor(hiddenSprite));

    auto mismatchedOwner = valid;
    mismatchedOwner.id.owner.objectIndex += 1;
    assert(!validateSpriteDescriptor(mismatchedOwner));
}
