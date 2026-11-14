#include "renderer/geode/RendererCoordinator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <chrono>
#include <cstdint>

using namespace geode::prelude;

class $modify(DashCoreExperimentalPlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        dashcore::renderer::geode::coordinator().onLevelInit();
    }

    void postUpdate(float dt) {
        auto const updateStart = std::chrono::steady_clock::now();
        PlayLayer::postUpdate(dt);
        auto const updateElapsed = std::chrono::steady_clock::now() - updateStart;
        auto const updateNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(updateElapsed).count()
        );
        dashcore::renderer::geode::coordinator().onFrame(this, updateNanoseconds);
    }

    void onExit() {
        dashcore::renderer::geode::coordinator().onLevelExit();
        PlayLayer::onExit();
    }
};
