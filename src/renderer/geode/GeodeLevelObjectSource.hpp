#pragma once

#include "renderer/core/LevelObjectSource.hpp"
#include "renderer/core/RendererDiagnostics.hpp"

#include <cstdint>

class PlayLayer;

namespace dashcore::renderer::geode {

struct GeodeCaptureStats {
    core::CaptureCounters counters{};
    std::uint64_t collectionNanoseconds = 0;
};

class GeodeLevelObjectSource final : public core::LevelObjectSource {
public:
    explicit GeodeLevelObjectSource(PlayLayer* layer) noexcept;

    bool collect(
        std::uint32_t levelGeneration,
        std::vector<core::SpriteDescriptor>& output
    ) const override;

    GeodeCaptureStats const& lastStats() const noexcept {
        return m_lastStats;
    }

private:
    PlayLayer* m_layer = nullptr;
    mutable GeodeCaptureStats m_lastStats{};
};

} // namespace dashcore::renderer::geode
