#pragma once

#include "renderer/core/LevelObjectCollector.hpp"
#include "renderer/core/MemoryBudget.hpp"
#include "renderer/core/RendererDiagnostics.hpp"
#include "renderer/geode/GeodeLevelObjectSource.hpp"

#include <cstdint>

class PlayLayer;

namespace dashcore::renderer::geode {

class RendererCoordinator {
public:
    void onLevelInit(PlayLayer* layer, bool captureEnabled) noexcept;
    void onFrame(PlayLayer* layer, std::uint64_t originalPostUpdateNanoseconds) noexcept;
    void onLevelExit() noexcept;

    core::RenderSnapshot const& snapshot() const noexcept {
        return m_snapshot;
    }

    bool active() const noexcept {
        return m_active;
    }

    core::RendererDiagnosticSummary summary() const noexcept {
        auto result = m_diagnostics.summary();
        result.captureEnabled = m_captureEnabled;
        result.memory = m_memorySnapshot;
        result.memoryBudget = m_memoryBudget;
        result.memorySampleCount = m_memorySampleCount;
        return result;
    }

private:
    void refreshMemoryBudget() noexcept;

    core::LevelObjectCollector m_collector;
    GeodeLevelObjectSource m_source;
    core::RenderSnapshot m_snapshot{};
    core::RendererDiagnostics m_diagnostics;
    core::ProcessMemorySnapshot m_memorySnapshot{};
    core::RendererMemoryBudget m_memoryBudget{};
    std::uint32_t m_generation = 0;
    std::uint64_t m_frameIndex = 0;
    std::uint64_t m_memorySampleCount = 0;
    bool m_captureEnabled = false;
    bool m_active = false;
};

RendererCoordinator& coordinator() noexcept;

} // namespace dashcore::renderer::geode
