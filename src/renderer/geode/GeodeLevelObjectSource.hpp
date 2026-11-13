#pragma once

#include "renderer/core/LevelObjectSource.hpp"

class PlayLayer;

namespace dashcore::renderer::geode {

class GeodeLevelObjectSource final : public core::LevelObjectSource {
public:
    explicit GeodeLevelObjectSource(PlayLayer* layer) noexcept;

    bool collect(
        std::uint32_t levelGeneration,
        std::vector<core::SpriteDescriptor>& output
    ) const override;

private:
    PlayLayer* m_layer = nullptr;
};

} // namespace dashcore::renderer::geode
