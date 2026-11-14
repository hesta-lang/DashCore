#pragma once

#include "renderer/core/LevelObjectSource.hpp"
#include "renderer/core/RendererDiagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class GameObject;
class PlayLayer;

namespace dashcore::renderer::geode {

struct GeodeCaptureStats {
    core::CaptureCounters counters{};
    std::uint64_t indexBuildNanoseconds = 0;
    std::uint64_t collectionNanoseconds = 0;
};

class GeodeLevelObjectSource final : public core::LevelObjectSource {
public:
    GeodeLevelObjectSource() noexcept = default;
    explicit GeodeLevelObjectSource(PlayLayer* layer) noexcept;

    void reset(PlayLayer* layer) noexcept;

    bool collect(
        std::uint32_t levelGeneration,
        std::vector<core::SpriteDescriptor>& output
    ) const override;

    GeodeCaptureStats const& lastStats() const noexcept {
        return m_lastStats;
    }

private:
    struct CachedRootObject {
        GameObject* object = nullptr;
        std::uint32_t objectIndex = 0;
    };

    bool rebuildObjectIndex() const;

    PlayLayer* m_layer = nullptr;
    mutable void const* m_indexedObjectsArray = nullptr;
    mutable std::size_t m_indexedObjectCount = 0;
    mutable bool m_indexBuilt = false;
    mutable std::vector<CachedRootObject> m_cachedRoots;
    mutable std::vector<CachedRootObject> m_frameRoots;
    mutable GeodeCaptureStats m_lastStats{};
};

} // namespace dashcore::renderer::geode
