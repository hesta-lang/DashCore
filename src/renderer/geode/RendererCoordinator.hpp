#pragma once

#include "renderer/core/LevelObjectCollector.hpp"
#include "renderer/core/RendererDiagnostics.hpp"
#include "renderer/geode/GeodeLevelObjectSource.hpp"
#include "bismuth_bridge/BismuthTriggerAdapter.hpp"

#include <cstdint>

class CheckpointObject;
class GJBaseGameLayer;
class PlayLayer;

namespace dashcore::renderer::geode {

class RendererCoordinator {
public:
    void onLevelInit(
        PlayLayer* layer,
        bool captureEnabled,
        bool triggerAdapterEnabled
    ) noexcept;
    void onFrame(PlayLayer* layer, std::uint64_t originalPostUpdateNanoseconds) noexcept;
    void onLevelExit() noexcept;
    void onLevelReset() noexcept;

    void onMoveActions(GJBaseGameLayer* layer) noexcept;
    void onRotationActions(GJBaseGameLayer* layer) noexcept;
    void onFollowActions(GJBaseGameLayer* layer) noexcept;
    void onToggleGroup(std::int32_t groupId, bool enabled) noexcept;
    void storeCheckpoint(CheckpointObject* checkpoint);
    void restoreCheckpoint(CheckpointObject* checkpoint) noexcept;

    core::RenderSnapshot const& snapshot() const noexcept {
        return m_snapshot;
    }

    bool active() const noexcept {
        return m_active;
    }

    bool triggerAdapterEnabled() const noexcept {
        return m_triggerAdapterEnabled && m_triggerAdapter.initialized();
    }

    bismuth_bridge::BismuthTriggerAdapter const& triggerAdapter() const noexcept {
        return m_triggerAdapter;
    }

    core::RendererDiagnosticSummary summary() const noexcept {
        auto result = m_diagnostics.summary();
        result.captureEnabled = m_captureEnabled;
        return result;
    }

private:
    core::LevelObjectCollector m_collector;
    GeodeLevelObjectSource m_source;
    core::RenderSnapshot m_snapshot{};
    core::RendererDiagnostics m_diagnostics;
    bismuth_bridge::BismuthTriggerAdapter m_triggerAdapter;
    std::uint32_t m_generation = 0;
    std::uint64_t m_frameIndex = 0;
    bool m_captureEnabled = false;
    bool m_triggerAdapterEnabled = false;
    bool m_active = false;
};

RendererCoordinator& coordinator() noexcept;

} // namespace dashcore::renderer::geode
