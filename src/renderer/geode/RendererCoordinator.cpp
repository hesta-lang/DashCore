#include "renderer/geode/RendererCoordinator.hpp"

#include "renderer/geode/GeodeLevelObjectSource.hpp"

#include <utility>

namespace dashcore::renderer::geode {

void RendererCoordinator::onLevelInit() noexcept {
    ++m_generation;
    if (m_generation == 0) m_generation = 1;
    m_collector.reset(m_generation);
    m_snapshot = {};
    m_frameIndex = 0;
    m_active = true;
}

void RendererCoordinator::onFrame(PlayLayer* layer) noexcept {
    if (!m_active || !layer) return;

    try {
        GeodeLevelObjectSource source(layer);
        core::RenderSnapshot next;
        if (m_collector.capture(source, m_frameIndex, next)) {
            m_snapshot = std::move(next);
        } else {
            m_snapshot = {};
        }
        ++m_frameIndex;
    } catch (...) {
        m_snapshot = {};
    }
}

void RendererCoordinator::onLevelExit() noexcept {
    m_snapshot = {};
    m_active = false;
    m_frameIndex = 0;
}

} // namespace dashcore::renderer::geode
