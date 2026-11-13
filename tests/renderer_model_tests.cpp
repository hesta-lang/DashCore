#include "renderer/core/BatchPlanner.hpp"
#include "renderer/core/GeometryBuilder.hpp"
#include "renderer/core/GlesCapabilities.hpp"
#include "renderer/core/LevelObjectCollector.hpp"
#include "renderer/core/SpriteDescriptorExtractor.hpp"
#include "renderer/core/SpriteQuadExtractor.hpp"

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

    std::vector<SpriteDescriptor> orderInput = {
        makeDescriptor(2, 10, 0, 1, 3),
        makeDescriptor(2, 11, 0, 0, 8),
        makeDescriptor(2, 12, 0, 0, 2),
        makeDescriptor(2, 13, 0, 1, 1),
    };
    auto order = buildStableDrawOrder(orderInput);
    assert((order == std::vector<std::uint32_t>{2, 1, 3, 0}));

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

    // CPU geometry preparation is pure and produces no draw calls.
    RenderSnapshot geometrySnapshot;
    geometrySnapshot.levelGeneration = 2;
    geometrySnapshot.frameIndex = 1;
    geometrySnapshot.sprites = {
        makeDescriptor(2, 40, 0, 0, 0)
    };
    auto geometryOrder = buildStableDrawOrder(geometrySnapshot.sprites);
    auto geometryBatches = planBatches(geometrySnapshot.sprites, geometryOrder);
    GeometryData geometry;
    assert(buildGeometry(geometrySnapshot, geometryBatches, geometry));
    assert(geometry.vertices.size() == 4);
    assert(geometry.indices.size() == 6);
    assert(geometry.vertices[0].x == -8.f);
    assert(geometry.vertices[0].y == -8.f);
    assert(geometry.vertices[0].u == 0.f);
    assert(geometry.vertices[0].v == 0.25f);
    assert(geometry.vertices[2].x == 8.f);
    assert(geometry.vertices[2].y == 8.f);
    assert(geometry.indices[0] == 0 && geometry.indices[1] == 1 && geometry.indices[2] == 2);
    assert(geometry.vertices[0].color.a == 255);

    geometrySnapshot.sprites[0].attributes.opacity = 128;
    assert(buildGeometry(geometrySnapshot, geometryBatches, geometry));
    assert(geometry.vertices[0].color.a == 128);

    auto nonFiniteGeometry = geometrySnapshot;
    nonFiniteGeometry.sprites[0].attributes.transform.values[4] =
        std::numeric_limits<float>::infinity();
    assert(!buildGeometry(nonFiniteGeometry, geometryBatches, geometry));
    assert(geometry.vertices.empty());

    RenderSnapshot emptySnapshot;
    assert(buildGeometry(emptySnapshot, {}, geometry));
    assert(geometry.vertices.empty());
    assert(geometry.indices.empty());

    assert(canRepresentSpriteCount(1));
    assert(!canRepresentSpriteCount(
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 4u) + 1u
    ));

    // GLES capabilities are parsed from the live version/extension strings;
    // compute and SSBO support are optional and never required by CPU prep.
    auto noContext = parseGlesCapabilities(nullptr, nullptr, false);
    assert(!noContext.contextAvailable);

    auto gles30 = parseGlesCapabilities(
        "OpenGL ES 3.0 Vendor",
        "GL_OES_vertex_array_object GL_EXT_color_buffer_float",
        true
    );
    assert(gles30.contextAvailable);
    assert(gles30.majorVersion == 3 && gles30.minorVersion == 0);
    assert(gles30.supportsBufferObjects);
    assert(gles30.supportsVertexArrayObjects);
    assert(!gles30.supportsComputeShaders);
    assert(!gles30.supportsShaderStorageBuffers);

    auto extensionCaps = parseGlesCapabilities(
        "OpenGL ES 3.0 Vendor",
        "GL_EXT_compute_shader GL_EXT_shader_storage_buffer_object",
        true
    );
    assert(extensionCaps.supportsComputeShaders);
    assert(extensionCaps.supportsShaderStorageBuffers);

    auto gles31 = parseGlesCapabilities("OpenGL ES 3.1 Vendor", "", true);
    assert(gles31.supportsComputeShaders);
    assert(gles31.supportsShaderStorageBuffers);
    // Live sprite quads are accepted only when this renderer can reproduce
    // their geometry, UV mapping, blend and uniform per-vertex color exactly.
    auto makeQuad = [] {
        SpriteQuadData quad{};
        ColorRGBA8 const color{18, 36, 72, 144};
        quad.bottomLeft = {-8.f, -4.f, 0.1f, 0.6f, color};
        quad.bottomRight = {8.f, -4.f, 0.5f, 0.6f, color};
        quad.topRight = {8.f, 4.f, 0.5f, 0.2f, color};
        quad.topLeft = {-8.f, 4.f, 0.1f, 0.2f, color};
        return quad;
    };

    RawSpriteAttributes quadAttributes{};
    auto const standardBlend = BlendState{1, 771, true};
    assert(extractAxisAlignedSpriteAttributes(
        makeQuad(), TextureKey{99}, standardBlend, true, quadAttributes
    ));
    assert(quadAttributes.textureKnown);
    assert(quadAttributes.texture.value == 99);
    assert(quadAttributes.localRect.x == -8.f);
    assert(quadAttributes.localRect.y == -4.f);
    assert(quadAttributes.localRect.width == 16.f);
    assert(quadAttributes.localRect.height == 8.f);
    assert(quadAttributes.uvRect.x == 0.1f);
    assert(quadAttributes.uvRect.y == 0.2f);
    assert(quadAttributes.uvRect.width == 0.4f);
    assert(quadAttributes.uvRect.height == 0.4f);
    assert(quadAttributes.color.r == 18 && quadAttributes.color.g == 36);
    assert(quadAttributes.color.b == 72 && quadAttributes.color.a == 144);
    assert(quadAttributes.opacity == 255);
    assert(quadAttributes.blend.sourceFactor == standardBlend.sourceFactor);
    assert(quadAttributes.blend.destinationFactor == standardBlend.destinationFactor);
    assert(quadAttributes.blend.supported);
    assert(quadAttributes.visible);

    auto rotatedUv = makeQuad();
    rotatedUv.bottomRight.u = 0.1f;
    rotatedUv.bottomRight.v = 0.2f;
    rotatedUv.topRight.u = 0.5f;
    rotatedUv.topRight.v = 0.6f;
    RawSpriteAttributes rejectedQuad{};
    assert(!extractAxisAlignedSpriteAttributes(
        rotatedUv, TextureKey{99}, standardBlend, true, rejectedQuad
    ));

    auto vertexTinted = makeQuad();
    vertexTinted.topLeft.color.r = 19;
    assert(!extractAxisAlignedSpriteAttributes(
        vertexTinted, TextureKey{99}, standardBlend, true, rejectedQuad
    ));

    auto sheared = makeQuad();
    sheared.topRight.x += 1.f;
    assert(!extractAxisAlignedSpriteAttributes(
        sheared, TextureKey{99}, standardBlend, true, rejectedQuad
    ));

    auto nanPosition = makeQuad();
    nanPosition.bottomLeft.x = std::numeric_limits<float>::quiet_NaN();
    assert(!extractAxisAlignedSpriteAttributes(
        nanPosition, TextureKey{99}, standardBlend, true, rejectedQuad
    ));

    assert(!extractAxisAlignedSpriteAttributes(
        makeQuad(), TextureKey{0}, standardBlend, true, rejectedQuad
    ));
    assert(!extractAxisAlignedSpriteAttributes(
        makeQuad(), TextureKey{99}, BlendState{1, 771, false}, true, rejectedQuad
    ));
    assert(!extractAxisAlignedSpriteAttributes(
        makeQuad(), TextureKey{99}, standardBlend, false, rejectedQuad
    ));

}
