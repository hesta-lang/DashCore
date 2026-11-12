#pragma once

#include "renderer/core/LevelObjectSource.hpp"

namespace dashcore::renderer::core {

class LevelObjectCollector {
public:
    static constexpr std::size_t kMaximumSprites = 250000;

    bool capture(
        LevelObjectSource const& source,
        std::uint64_t frameIndex,
        RenderSnapshot& output
    ) const;

    void reset(std::uint32_t nextLevelGeneration) noexcept;

    std::uint32_t levelGeneration() const noexcept {
        return m_levelGeneration;
    }

private:
    std::uint32_t m_levelGeneration = 1;
};

} // namespace dashcore::renderer::core
