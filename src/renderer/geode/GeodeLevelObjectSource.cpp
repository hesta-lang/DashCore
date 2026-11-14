#include "renderer/geode/GeodeLevelObjectSource.hpp"

#include "renderer/core/SpriteDescriptorExtractor.hpp"
#include "renderer/core/LevelObjectCollector.hpp"
#include "renderer/core/SpriteQuadExtractor.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GameObject.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace dashcore::renderer::geode {
namespace {

constexpr std::size_t kMaximumNodes = 500000;
constexpr std::size_t kMaximumDepth = 128;
constexpr std::size_t kMaximumChildrenPerNode = 100000;
constexpr float kTransformProbeDistance = 1.f;

struct CollectionTimer {
    std::chrono::steady_clock::time_point const start = std::chrono::steady_clock::now();
    std::uint64_t& destination;

    explicit CollectionTimer(std::uint64_t& target) noexcept : destination(target) {}

    ~CollectionTimer() {
        auto const elapsed = std::chrono::steady_clock::now() - start;
        destination = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
        );
    }
};

enum class SpriteRejectReason {
    None,
    Invisible,
    Flags,
    Batched,
    MissingTexture,
    UnuploadedTexture,
    Blend,
    Quad,
    Transform,
};

struct RootObject {
    GameObject* object = nullptr;
    std::uint32_t objectIndex = 0;
};

struct CaptureState {
    PlayLayer* layer = nullptr;
    std::uint32_t generation = 0;
    std::uint32_t traversalOrder = 0;
    std::unordered_set<cocos2d::CCNode*> visited;
    std::unordered_set<std::uint64_t> textureKeys;
    core::CaptureCounters* counters = nullptr;
    std::vector<core::SpriteDescriptor>* output = nullptr;
};

void countRejected(
    core::CaptureCounters& counters,
    SpriteRejectReason reason
) noexcept {
    switch (reason) {
        case SpriteRejectReason::None:
            break;
        case SpriteRejectReason::Invisible:
            ++counters.invisibleSubtrees;
            break;
        case SpriteRejectReason::Flags:
            ++counters.rejectedFlags;
            break;
        case SpriteRejectReason::Batched:
            ++counters.rejectedBatched;
            break;
        case SpriteRejectReason::MissingTexture:
            ++counters.rejectedMissingTexture;
            break;
        case SpriteRejectReason::UnuploadedTexture:
            ++counters.rejectedUnuploadedTexture;
            break;
        case SpriteRejectReason::Blend:
            ++counters.rejectedBlend;
            break;
        case SpriteRejectReason::Quad:
            ++counters.rejectedQuad;
            break;
        case SpriteRejectReason::Transform:
            ++counters.rejectedTransform;
            break;
    }
}

bool supportedBlendFactor(std::uint32_t factor) noexcept {
    switch (factor) {
        case 0x0000u:
        case 0x0001u:
        case 0x0300u:
        case 0x0301u:
        case 0x0302u:
        case 0x0303u:
        case 0x0304u:
        case 0x0305u:
        case 0x0306u:
        case 0x0307u:
        case 0x0308u:
            return true;
        default:
            return false;
    }
}

bool copyBlend(cocos2d::CCSprite* sprite, core::BlendState& output) noexcept {
    if (!sprite) return false;
    auto const blend = sprite->getBlendFunc();
    auto const source = static_cast<std::uint32_t>(blend.src);
    auto const destination = static_cast<std::uint32_t>(blend.dst);
    if (!supportedBlendFactor(source) ||
        !supportedBlendFactor(destination) ||
        destination == 0x0308u) {
        return false;
    }
    output = {source, destination, true};
    return true;
}

core::SpriteQuadVertexData copyVertex(cocos2d::ccV3F_C4B_T2F const& vertex) noexcept {
    return {
        vertex.vertices.x,
        vertex.vertices.y,
        vertex.texCoords.u,
        vertex.texCoords.v,
        {
            vertex.colors.r,
            vertex.colors.g,
            vertex.colors.b,
            vertex.colors.a
        },
        vertex.vertices.z
    };
}

SpriteRejectReason copySpriteAttributes(
    cocos2d::CCSprite* sprite,
    PlayLayer* layer,
    core::RawSpriteAttributes& output
) noexcept {
    if (!sprite || !layer || !sprite->isVisible()) {
        return SpriteRejectReason::Invisible;
    }
    if (sprite->m_bShouldBeHidden ||
        sprite->m_bDontDraw ||
        sprite->m_bUseVertexMod ||
        sprite->m_bRectRotated ||
        sprite->isFlipX() ||
        sprite->isFlipY()) {
        return SpriteRejectReason::Flags;
    }
    if (sprite->getBatchNode() != nullptr) return SpriteRejectReason::Batched;

    auto* texture = sprite->getTexture();
    if (!texture) return SpriteRejectReason::MissingTexture;
    auto const textureName = texture->getName();
    if (textureName == 0u) return SpriteRejectReason::UnuploadedTexture;

    core::BlendState blend{};
    if (!copyBlend(sprite, blend)) return SpriteRejectReason::Blend;

    auto const quad = sprite->getQuad();
    core::SpriteQuadData copiedQuad{
        copyVertex(quad.bl),
        copyVertex(quad.br),
        copyVertex(quad.tr),
        copyVertex(quad.tl)
    };

    core::RawSpriteAttributes attributes{};
    if (!core::extractAxisAlignedSpriteAttributes(
            copiedQuad,
            core::TextureKey{static_cast<std::uint64_t>(textureName)},
            blend,
            true,
            attributes)) {
        return SpriteRejectReason::Quad;
    }

    auto toLayer = [sprite, layer](cocos2d::CCPoint const& point) {
        return layer->convertToNodeSpace(sprite->convertToWorldSpace(point));
    };
    auto const origin = toLayer({0.f, 0.f});
    auto const axisX = toLayer({kTransformProbeDistance, 0.f});
    auto const axisY = toLayer({0.f, kTransformProbeDistance});
    attributes.transform.values = {
        (axisX.x - origin.x) / kTransformProbeDistance,
        (axisX.y - origin.y) / kTransformProbeDistance,
        (axisY.x - origin.x) / kTransformProbeDistance,
        (axisY.y - origin.y) / kTransformProbeDistance,
        origin.x,
        origin.y
    };

    core::SpriteDescriptor validation{};
    validation.owner = {1, 0};
    validation.id = {validation.owner, 0};
    validation.attributes = attributes;
    if (!core::validateSpriteDescriptor(validation)) {
        return SpriteRejectReason::Transform;
    }

    output = attributes;
    return SpriteRejectReason::None;
}

bool captureNode(
    cocos2d::CCNode* node,
    std::size_t depth,
    std::uint32_t objectIndex,
    std::uint32_t& nextChildIndex,
    CaptureState& state
) {
    if (!node) return true;
    if (depth > kMaximumDepth) {
        ++state.counters->depthLimitHits;
        return false;
    }
    if (!state.visited.insert(node).second) {
        ++state.counters->duplicateNodeReferences;
        return true;
    }
    ++state.counters->nodesVisited;
    if (state.counters->nodesVisited > kMaximumNodes) {
        ++state.counters->nodeLimitHits;
        return false;
    }
    if (!node->isVisible()) {
        ++state.counters->invisibleSubtrees;
        return true;
    }

    if (typeinfo_cast<cocos2d::CCSpriteBatchNode*>(node)) return true;

    ::geode::cocos::CCArrayExt<cocos2d::CCNode*, false> childArray(node->getChildren());
    if (childArray.size() > kMaximumChildrenPerNode) {
        ++state.counters->childrenLimitHits;
        return false;
    }

    std::vector<cocos2d::CCNode*> children;
    children.reserve(childArray.size());
    for (auto* child : childArray) {
        if (child) children.push_back(child);
    }

    std::stable_sort(children.begin(), children.end(), [](auto* left, auto* right) {
        if (left->getZOrder() != right->getZOrder()) {
            return left->getZOrder() < right->getZOrder();
        }
        return left->getOrderOfArrival() < right->getOrderOfArrival();
    });

    for (auto* child : children) {
        if (child->getZOrder() < 0 &&
            !captureNode(child, depth + 1, objectIndex, nextChildIndex, state)) {
            return false;
        }
    }

    if (auto* sprite = typeinfo_cast<cocos2d::CCSprite*>(node)) {
        ++state.counters->candidateSprites;
        core::RawSpriteAttributes attributes{};
        auto const rejection = copySpriteAttributes(sprite, state.layer, attributes);
        if (rejection != SpriteRejectReason::None) {
            countRejected(*state.counters, rejection);
        } else {
            if (state.output->size() >= core::LevelObjectCollector::kMaximumSprites ||
                state.traversalOrder == std::numeric_limits<std::uint32_t>::max() ||
                nextChildIndex == std::numeric_limits<std::uint32_t>::max()) {
                ++state.counters->sourceFailures;
                return false;
            }

            core::SpriteDescriptor descriptor{};
            if (core::extractSpriteDescriptor(
                    attributes,
                    {state.generation, objectIndex},
                    nextChildIndex,
                    {0, state.traversalOrder},
                    descriptor)) {
                state.output->push_back(std::move(descriptor));
                state.textureKeys.insert(attributes.texture.value);
                ++nextChildIndex;
                ++state.traversalOrder;
                ++state.counters->acceptedSprites;
            } else {
                ++state.counters->rejectedDescriptor;
            }
        }
    }

    for (auto* child : children) {
        if (child->getZOrder() >= 0 &&
            !captureNode(child, depth + 1, objectIndex, nextChildIndex, state)) {
            return false;
        }
    }
    return true;
}

} // namespace

GeodeLevelObjectSource::GeodeLevelObjectSource(PlayLayer* layer) noexcept
  : m_layer(layer) {}

bool GeodeLevelObjectSource::collect(
    std::uint32_t levelGeneration,
    std::vector<core::SpriteDescriptor>& output
) const {
    output.clear();
    m_lastStats = {};
    CollectionTimer timer(m_lastStats.collectionNanoseconds);

    auto fail = [this, &output]() {
        ++m_lastStats.counters.sourceFailures;
        output.clear();
        return false;
    };

    if (!m_layer || !m_layer->m_objects || !m_layer->m_objectLayer) return fail();

    ::geode::cocos::CCArrayExt<GameObject, false> objects(m_layer->m_objects);
    if (objects.size() > core::LevelObjectCollector::kMaximumSprites) return fail();

    std::vector<RootObject> roots;
    roots.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto* object = objects[index];
        if (!object || object->getParent() != m_layer->m_objectLayer) continue;
        if (index > std::numeric_limits<std::uint32_t>::max()) return fail();
        roots.push_back({object, static_cast<std::uint32_t>(index)});
    }

    std::stable_sort(roots.begin(), roots.end(), [](RootObject const& left, RootObject const& right) {
        if (left.object->getZOrder() != right.object->getZOrder()) {
            return left.object->getZOrder() < right.object->getZOrder();
        }
        if (left.object->getOrderOfArrival() != right.object->getOrderOfArrival()) {
            return left.object->getOrderOfArrival() < right.object->getOrderOfArrival();
        }
        return left.objectIndex < right.objectIndex;
    });

    m_lastStats.counters.rootObjects = roots.size();

    CaptureState state{};
    state.layer = m_layer;
    state.generation = levelGeneration;
    state.counters = &m_lastStats.counters;
    state.output = &output;

    for (auto const& root : roots) {
        std::uint32_t nextChildIndex = 0;
        if (!captureNode(root.object, 0, root.objectIndex, nextChildIndex, state)) {
            output.clear();
            if (m_lastStats.counters.sourceFailures == 0) {
                ++m_lastStats.counters.sourceFailures;
            }
            return false;
        }
    }

    m_lastStats.counters.uniqueTextures = state.textureKeys.size();
    return true;
}

} // namespace dashcore::renderer::geode
