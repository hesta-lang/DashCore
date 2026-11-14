#include "bismuth_bridge/BismuthBridge.hpp"

#include "renderer/gles/GLESBackend.hpp"

#include <Geode/Enums.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/cocos/sprite_nodes/CCSprite.h>
#include <Geode/cocos/sprite_nodes/CCSpriteBatchNode.h>
#include <Geode/utils/cocos.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

using namespace geode::prelude;

namespace dashcore::bismuth_bridge {
namespace {

constexpr std::size_t kMaximumSpriteDepth = 128;
constexpr std::size_t kMaximumSpritesPerObject = 250000;

constexpr std::array<ZLayer, 9> kBismuthZLayers = {
    ZLayer::B5,
    ZLayer::B4,
    ZLayer::B3,
    ZLayer::B2,
    ZLayer::B1,
    ZLayer::T1,
    ZLayer::T2,
    ZLayer::T3,
    ZLayer::T4
};

std::int32_t normalizeObjectZLayer(std::int32_t value) noexcept {
    if ((value % 2) == 0) {
        --value;
        auto const minimum = static_cast<std::int32_t>(ZLayer::B5);
        if (value < minimum) value = minimum;
    }
    return value;
}

SpriteSheet sheetFor(GameObject* object, SpriteType type) noexcept {
    if (type == SpriteType::Glow) return SpriteSheet::Glow;
    return static_cast<SpriteSheet>(object->getParentMode());
}

struct SpriteTraversal {
    GameObject* object = nullptr;
    std::uint32_t objectIndex = 0;
    std::uint32_t nextSpriteIndex = 0;
    std::unordered_set<cocos2d::CCSprite*> visited;
    SpriteVisitor visitor = nullptr;
    void* userData = nullptr;
};

bool visitSprite(
    cocos2d::CCSprite* sprite,
    cocos2d::CCAffineTransform parentTransform,
    SpriteType inheritedType,
    std::size_t depth,
    SpriteTraversal& state
) {
    if (!sprite) return true;
    if (depth > kMaximumSpriteDepth ||
        state.nextSpriteIndex >= kMaximumSpritesPerObject) {
        return false;
    }
    if (!state.visited.insert(sprite).second) return true;

    auto const transform = cocos2d::CCAffineTransformConcat(
        sprite->nodeToParentTransform(),
        parentTransform
    );

    auto type = inheritedType;
    if (sprite == state.object->m_colorSprite) type = SpriteType::Detail;
    if (sprite == state.object->m_glowSprite) type = SpriteType::Glow;

    auto drawType = type;
    if (sprite == state.object) {
        if (state.object->m_isObjectBlack) drawType = SpriteType::Black;
    } else if (
        type == SpriteType::Base &&
        state.object->m_isColorSpriteBlack &&
        sprite->getTag() != 997
    ) {
        drawType = SpriteType::BlackChild;
    }

    // Match Cocos/Bismuth child order: negative-z children, this sprite,
    // then non-negative-z children. Do not sort or mutate the live array.
    ::geode::cocos::CCArrayExt<cocos2d::CCSprite*, false> children(
        sprite->getChildren()
    );

    for (auto* child : children) {
        if (child && child->getZOrder() < 0 &&
            !visitSprite(child, transform, type, depth + 1, state)) {
            return false;
        }
    }

    if (!sprite->getDontDraw()) {
        SpriteView view{};
        view.sprite = sprite;
        view.owner = state.object;
        view.type = drawType;
        view.spriteSheet = sheetFor(state.object, type);
        view.objectToParentTransform = transform;
        view.objectIndex = state.objectIndex;
        view.spriteIndex = state.nextSpriteIndex++;
        if (!state.visitor(view, state.userData)) return false;
    }

    for (auto* child : children) {
        if (child && child->getZOrder() >= 0 &&
            !visitSprite(child, transform, type, depth + 1, state)) {
            return false;
        }
    }

    return true;
}

} // namespace

bool enumerateObjects(
    cocos2d::CCArray* objects,
    ObjectVisitor visitor,
    void* userData
) {
    if (!objects || !visitor) return false;

    std::uint32_t objectIndex = 0;
    ::geode::cocos::CCArrayExt<GameObject*, false> entries(objects);
    for (auto* object : entries) {
        if (object) {
            auto const zLayer = static_cast<std::int32_t>(object->getObjectZLayer());
            ObjectView view{};
            view.object = object;
            view.objectIndex = objectIndex;
            view.objectZLayer = zLayer;
            view.normalizedObjectZLayer = normalizeObjectZLayer(zLayer);
            view.objectDrawOrder = static_cast<std::int32_t>(object->getObjectZOrder());
            view.parentMode = static_cast<std::int32_t>(object->getParentMode());
            view.baseOrDetailBlending = object->m_baseOrDetailBlending;
            view.hasRenderableGlow = object->m_glowSprite != nullptr && !object->m_hasNoGlow;
            if (!visitor(view, userData)) return false;
        }

        if (objectIndex == UINT32_MAX) return false;
        ++objectIndex;
    }
    return true;
}

bool enumerateObjectSprites(
    GameObject* object,
    std::uint32_t objectIndex,
    SpriteVisitor visitor,
    void* userData
) {
    if (!object || !visitor) return false;

    SpriteTraversal state{};
    state.object = object;
    state.objectIndex = objectIndex;
    state.visitor = visitor;
    state.userData = userData;
    state.visited.reserve(64);

    auto const identity = cocos2d::CCAffineTransformMakeIdentity();

    if (object->m_glowSprite &&
        !visitSprite(object->m_glowSprite, identity, SpriteType::Base, 0, state)) {
        return false;
    }

    auto* colorSprite = object->m_colorSprite;
    auto const hasSeparateColorSprite =
        colorSprite && colorSprite->getParent() != object;
    auto const colorSpriteIsInFront = object->m_colorZLayerRelated;

    if (hasSeparateColorSprite && !colorSpriteIsInFront &&
        !visitSprite(colorSprite, identity, SpriteType::Base, 0, state)) {
        return false;
    }

    if (!visitSprite(object, identity, SpriteType::Base, 0, state)) return false;

    if (hasSeparateColorSprite && colorSpriteIsInFront &&
        !visitSprite(colorSprite, identity, SpriteType::Base, 0, state)) {
        return false;
    }

    return true;
}

bool enumerateBatchLayers(
    GJBaseGameLayer* layer,
    BatchLayerVisitor visitor,
    void* userData
) {
    if (!layer || !visitor) return false;

    for (auto const zLayer : kBismuthZLayers) {
        for (std::int32_t sheetIndex = 0;
             sheetIndex < static_cast<std::int32_t>(SpriteSheet::Count);
             ++sheetIndex) {
            auto const sheet = static_cast<SpriteSheet>(sheetIndex);
            if (sheet == SpriteSheet::Special || sheet == SpriteSheet::Unknown) {
                continue;
            }

            for (bool blending : {false, true}) {
                auto* const parent = layer->parentForZLayer(
                    static_cast<std::int32_t>(zLayer),
                    blending,
                    sheetIndex,
                    false
                );
                auto* const node =
                    typeinfo_cast<cocos2d::CCSpriteBatchNode*>(parent);
                if (!node) continue;

                BatchLayerView view{};
                view.node = node;
                view.zLayer = static_cast<std::int32_t>(zLayer);
                view.spriteSheet = sheet;
                view.blending = blending;
                view.nodeZOrder = node->getZOrder();
                view.nodeArrivalOrder = node->getOrderOfArrival();
                if (!visitor(view, userData)) return false;
            }
        }
    }

    return true;
}

renderer::core::GlesCapabilities queryCurrentGlesCapabilities() noexcept {
    return renderer::gles::queryCurrentContextCapabilities();
}

} // namespace dashcore::bismuth_bridge
