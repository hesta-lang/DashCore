#include "renderer/core/BatchPlanner.hpp"
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
    std::int32_t zLayer,
    std::uint32_t traversalOrder,
    std::uint64_t texture = 42,
    BlendState blend = {1, 771, true}
) {
    SpriteDescriptor value{};
    value.owner = {generation, objectIndex};
    value.id = {value.owner, childIndex};
    value.order = {zLayer, traversalOrder};
    value.attributes = validAttributes(texture);
    value.attributes.blend = blend;
    return value;
}

int main() {
    auto valid = makeDescriptor(1, 7, 2, 0, 10);
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

    LevelObjectCollector collector;
    FakeSource source;
    RenderSnapshot snapshot{};

    assert(collector.capture(source, 10, snapshot));
    assert(snapshot.levelGeneration == 1);
    assert(snapshot.frameIndex == 10);
    assert(snapshot.sprites.empty());

    source.descriptors = {
        makeDescriptor(1, 3, 0, 0, 0),
        makeDescriptor(1, 3, 1, 0, 1),
        makeDescriptor(1, 9, 0, 1, 2),
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

    source.descriptors = {makeDescriptor(1, 3, 0, 0, 0), makeDescriptor(1, 3, 0, 0, 1)};
    assert(!collector.capture(source, 14, snapshot));
    assert(snapshot.sprites.empty());

    source.descriptors = {makeDescriptor(1, 3, 0, 0, 0)};
    collector.reset(2);
    assert(collector.levelGeneration() == 2);
    assert(!collector.capture(source, 15, snapshot));
    assert(snapshot.sprites.empty());

    source.descriptors = {makeDescriptor(2, 3, 0, 0, 0)};
    assert(collector.capture(source, 16, snapshot));
    assert(snapshot.levelGeneration == 2);
    assert(snapshot.sprites[0].owner.levelGeneration == 2);

    collector.reset(0);
    assert(collector.levelGeneration() == 1);

    // Ordering: z-layer first, then explicit traversal order; stable for ties.
    std::vector<SpriteDescriptor> orderInput = {
        makeDescriptor(2, 10, 0, 1, 3),
        makeDescriptor(2, 11, 0, 0, 8),
        makeDescriptor(2, 12, 0, 0, 2),
        makeDescriptor(2, 13, 0, 1, 1),
    };
    auto order = buildStableDrawOrder(orderInput);
    assert((order == std::vector<std::uint32_t>{2, 1, 3, 0}));

    // Contiguous-only batching: A,A,B,A must remain three batches.
    std::vector<SpriteDescriptor> batchInput = {
        makeDescriptor(2, 20, 0, 0, 0, 42, {1, 771, true}),
        makeDescriptor(2, 21, 0, 0, 1, 42, {1, 771, true}),
        makeDescriptor(2, 22, 0, 0, 2, 43, {1, 771, true}),
        makeDescriptor(2, 23, 0, 0, 3, 42, {1, 771, true}),
        makeDescriptor(2, 24, 0, 0, 4, 42, {770, 771, true}),
        makeDescriptor(2, 25, 0, 1, 5, 42, {770, 771, true}),
    };
    std::vector<std::uint32_t> explicitOrder{0, 1, 2, 3, 4, 5};
    auto batches = planBatches(batchInput, explicitOrder);
    assert(batches.size() == 5);
    assert(batches[0].firstOrderIndex == 0);
    assert((batches[0].spriteIndices == std::vector<std::uint32_t>{0, 1}));
    assert((batches[1].spriteIndices == std::vector<std::uint32_t>{2}));
    assert((batches[2].spriteIndices == std::vector<std::uint32_t>{3}));
    assert((batches[3].spriteIndices == std::vector<std::uint32_t>{4}));
    assert((batches[4].spriteIndices == std::vector<std::uint32_t>{5}));

    std::vector<std::uint32_t> invalidIndex{0, 99};
    assert(planBatches(batchInput, invalidIndex).empty());
    std::vector<std::uint32_t> duplicateIndex{0, 0};
    assert(planBatches(batchInput, duplicateIndex).empty());

    auto unsupportedInOrder = batchInput;
    unsupportedInOrder[1].attributes.textureKnown = false;
    assert(buildStableDrawOrder(unsupportedInOrder).empty());
}
