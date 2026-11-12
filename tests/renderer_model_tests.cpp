#include "renderer/core/LevelObjectCollector.hpp"
#include "renderer/core/SpriteDescriptorExtractor.hpp"

#include <cassert>
#include <limits>
#include <vector>

using namespace dashcore::renderer::core;

class FakeSource final : public LevelObjectSource {
public:
    std::vector<SpriteDescriptor> descriptors;
    bool complete = true;
    mutable std::uint32_t lastGeneration = 0;

    bool collect(
        std::uint32_t generation,
        std::vector<SpriteDescriptor>& output
    ) const override {
        lastGeneration = generation;
        if (!complete) return false;
        output = descriptors;
        return true;
    }
};

static RawSpriteAttributes validAttributes(std::uint64_t texture = 42) {
    RawSpriteAttributes value{};
    value.texture = {texture};
    value.textureKnown = true;
    value.uvRect = {0.f, 0.f, 0.25f, 0.25f};
    value.localRect = {-8.f, -8.f, 16.f, 16.f};
    value.blend = {1, 771, true};
    return value;
}

static SpriteDescriptor makeDescriptor(
    std::uint32_t generation,
    std::uint32_t objectIndex,
    std::uint32_t childIndex,
    std::uint32_t traversalOrder
) {
    SpriteDescriptor value{};
    value.owner = {generation, objectIndex};
    value.id = {value.owner, childIndex};
    value.order = {0, traversalOrder};
    value.attributes = validAttributes();
    return value;
}

int main() {
    // Descriptor extraction and validation.
    auto valid = makeDescriptor(1, 7, 2, 10);
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

    SpriteDescriptor extracted{};
    assert(extractSpriteDescriptor(valid.attributes, valid.owner, 4, {1, 22}, extracted));
    assert(extracted.id.childIndex == 4);
    assert(extracted.order.zLayer == 1);
    assert(extracted.order.traversalOrder == 22);

    auto changed = valid.attributes;
    changed.transform.values[4] = 120.f;
    changed.color.r = 17;
    changed.opacity = 91;
    assert(extractSpriteDescriptor(changed, valid.owner, 5, {1, 23}, extracted));
    assert(extracted.attributes.transform.values[4] == 120.f);
    assert(extracted.attributes.color.r == 17);
    assert(extracted.attributes.opacity == 91);

    // Collector with fake sources: empty levels, multiple sprites per object,
    // generation changes, repeat captures, and safe failure behavior.
    LevelObjectCollector collector;
    FakeSource source;
    RenderSnapshot snapshot{};

    assert(collector.capture(source, 10, snapshot));
    assert(snapshot.levelGeneration == 1);
    assert(snapshot.frameIndex == 10);
    assert(snapshot.sprites.empty());

    source.descriptors = {
        makeDescriptor(1, 3, 0, 0),
        makeDescriptor(1, 3, 1, 1),
        makeDescriptor(1, 9, 0, 2),
    };
    assert(collector.capture(source, 11, snapshot));
    assert(source.lastGeneration == 1);
    assert(snapshot.sprites.size() == 3);
    assert(snapshot.sprites[0].owner.objectIndex == 3);
    assert(snapshot.sprites[1].id.childIndex == 1);
    assert(snapshot.sprites[2].owner.objectIndex == 9);

    auto nextFrame = source.descriptors;
    nextFrame[0].attributes.transform.values[4] = 55.f;
    source.descriptors = nextFrame;
    assert(collector.capture(source, 12, snapshot));
    assert(snapshot.frameIndex == 12);
    assert(snapshot.sprites[0].attributes.transform.values[4] == 55.f);

    source.complete = false;
    assert(!collector.capture(source, 13, snapshot));
    assert(snapshot.sprites.empty());
    source.complete = true;

    source.descriptors = {makeDescriptor(1, 3, 0, 0), makeDescriptor(1, 3, 0, 1)};
    assert(!collector.capture(source, 14, snapshot));
    assert(snapshot.sprites.empty());

    source.descriptors = {makeDescriptor(1, 3, 0, 0)};
    collector.reset(2);
    assert(collector.levelGeneration() == 2);
    assert(!collector.capture(source, 15, snapshot));
    assert(snapshot.sprites.empty());

    source.descriptors = {makeDescriptor(2, 3, 0, 0)};
    assert(collector.capture(source, 16, snapshot));
    assert(snapshot.levelGeneration == 2);
    assert(snapshot.sprites[0].owner.levelGeneration == 2);

    collector.reset(0);
    assert(collector.levelGeneration() == 1);
}
