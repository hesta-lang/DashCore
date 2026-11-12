#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dashcore::renderer::core {

struct ObjectId {
    std::uint32_t levelGeneration = 0;
    std::uint32_t objectIndex = 0;
    friend bool operator==(ObjectId const&, ObjectId const&) = default;
};

struct SpriteId {
    ObjectId owner{};
    std::uint32_t childIndex = 0;
    friend bool operator==(SpriteId const&, SpriteId const&) = default;
};

struct TextureKey {
    std::uint64_t value = 0;
};

struct RectF {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
};

struct Transform2D {
    std::array<float, 6> values{1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
};

struct ColorRGBA8 {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct BlendState {
    std::uint32_t sourceFactor = 0;
    std::uint32_t destinationFactor = 0;
    bool supported = false;
};

struct LayerOrder {
    std::int32_t zLayer = 0;
    std::uint32_t traversalOrder = 0;
};

// These are copied values only. No source scene pointers are retained.
struct RawSpriteAttributes {
    TextureKey texture{};
    bool textureKnown = false;
    RectF uvRect{};
    RectF localRect{};
    Transform2D transform{};
    ColorRGBA8 color{};
    std::uint8_t opacity = 255;
    BlendState blend{};
    bool visible = true;
};

struct SpriteDescriptor {
    ObjectId owner{};
    SpriteId id{};
    LayerOrder order{};
    RawSpriteAttributes attributes{};
};

struct RenderSnapshot {
    std::uint32_t levelGeneration = 0;
    std::uint64_t frameIndex = 0;
    std::vector<SpriteDescriptor> sprites;
};

bool validateSpriteDescriptor(SpriteDescriptor const& descriptor) noexcept;

} // namespace dashcore::renderer::core
