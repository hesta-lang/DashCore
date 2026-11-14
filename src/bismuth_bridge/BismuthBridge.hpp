#pragma once

#include "renderer/core/GlesCapabilities.hpp"

#include <Geode/cocos/cocoa/CCAffineTransform.h>

#include <cstdint>

class GameObject;
class GJBaseGameLayer;

namespace cocos2d {
class CCArray;
class CCSprite;
class CCSpriteBatchNode;
}

namespace dashcore::bismuth_bridge {

// Numeric values intentionally match the SpriteType ordering used by Bismuth.
enum class SpriteType : std::uint8_t {
    Base = 0,
    Detail = 1,
    Black = 2,
    Glow = 3,
    BlackChild = 4
};

// Numeric values intentionally match GameObject::getParentMode() / Bismuth.
enum class SpriteSheet : std::uint8_t {
    Game1 = 0,
    Game2 = 1,
    Text = 2,
    Fire = 3,
    Special = 4,
    Glow = 5,
    Pixel = 6,
    Unknown = 7,
    Particle = 8,
    Count = 9
};

// Engine pointers in these views are borrowed and valid only for the duration
// of the visitor call. The bridge never retains or mutates them.
struct ObjectView {
    GameObject* object = nullptr;
    std::uint32_t objectIndex = 0;
    std::int32_t objectZLayer = 0;
    std::int32_t normalizedObjectZLayer = 0;
    std::int32_t objectDrawOrder = 0;
    std::int32_t parentMode = 0;
    bool baseOrDetailBlending = false;
    bool hasRenderableGlow = false;
};

struct SpriteView {
    cocos2d::CCSprite* sprite = nullptr;
    GameObject* owner = nullptr;
    SpriteType type = SpriteType::Base;
    SpriteSheet spriteSheet = SpriteSheet::Game1;
    cocos2d::CCAffineTransform objectToParentTransform{};
    std::uint32_t objectIndex = 0;
    std::uint32_t spriteIndex = 0;
};

struct BatchLayerView {
    cocos2d::CCSpriteBatchNode* node = nullptr;
    std::int32_t zLayer = 0;
    SpriteSheet spriteSheet = SpriteSheet::Game1;
    bool blending = false;
    std::int32_t nodeZOrder = 0;
    std::uint32_t nodeArrivalOrder = 0;
};

using ObjectVisitor = bool (*)(ObjectView const&, void*);
using SpriteVisitor = bool (*)(SpriteView const&, void*);
using BatchLayerVisitor = bool (*)(BatchLayerView const&, void*);

// Returns false for invalid input or when a visitor stops enumeration.
// Object indices preserve the source CCArray indices, including null slots.
bool enumerateObjects(
    cocos2d::CCArray* objects,
    ObjectVisitor visitor,
    void* userData
);

// Unpacks glow, detail/color, base, black and child sprites in Bismuth's
// original traversal order. No Cocos draw/visit hooks are installed.
bool enumerateObjectSprites(
    GameObject* object,
    std::uint32_t objectIndex,
    SpriteVisitor visitor,
    void* userData
);

// Exposes the existing Cocos batch nodes for Bismuth's ZLayer/sheet/blend
// classification. It never creates, removes, hides or reorders batch nodes.
bool enumerateBatchLayers(
    GJBaseGameLayer* layer,
    BatchLayerVisitor visitor,
    void* userData
);

// Must be called on the game's render thread while the original Cocos EGL
// context is current. This is a query only; it never creates or changes EGL.
renderer::core::GlesCapabilities queryCurrentGlesCapabilities() noexcept;

} // namespace dashcore::bismuth_bridge
