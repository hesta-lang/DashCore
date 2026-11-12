#include "renderer/core/LevelObjectCollector.hpp"

#include <set>
#include <utility>

namespace dashcore::renderer::core {

bool LevelObjectCollector::capture(
    LevelObjectSource const& source,
    std::uint64_t frameIndex,
    RenderSnapshot& output
) const {
    std::vector<SpriteDescriptor> descriptors;
    if (!source.collect(m_levelGeneration, descriptors) ||
        descriptors.size() > kMaximumSprites) {
        output = {};
        return false;
    }

    std::set<std::pair<std::uint32_t, std::uint32_t>> seenSprites;
    for (auto const& descriptor : descriptors) {
        if (descriptor.owner.levelGeneration != m_levelGeneration ||
            descriptor.id.owner.levelGeneration != m_levelGeneration ||
            !validateSpriteDescriptor(descriptor)) {
            output = {};
            return false;
        }

        auto const key = std::pair{
            descriptor.id.owner.objectIndex,
            descriptor.id.childIndex
        };
        if (!seenSprites.insert(key).second) {
            output = {};
            return false;
        }
    }

    RenderSnapshot candidate;
    candidate.levelGeneration = m_levelGeneration;
    candidate.frameIndex = frameIndex;
    candidate.sprites = std::move(descriptors);
    output = std::move(candidate);
    return true;
}

void LevelObjectCollector::reset(std::uint32_t nextLevelGeneration) noexcept {
    m_levelGeneration = nextLevelGeneration == 0 ? 1 : nextLevelGeneration;
}

} // namespace dashcore::renderer::core
