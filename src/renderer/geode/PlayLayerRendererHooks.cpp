#include "renderer/geode/RendererCoordinator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(DashCoreExperimentalPlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        dashcore::renderer::geode::coordinator().onLevelInit();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        dashcore::renderer::geode::coordinator().onFrame(this);
    }

    void onExit() {
        dashcore::renderer::geode::coordinator().onLevelExit();
        PlayLayer::onExit();
    }
};
