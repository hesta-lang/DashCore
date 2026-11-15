#include "renderer/geode/ExperimentalRendererModule.hpp"
#include "renderer/geode/RendererCoordinator.hpp"

#include <Geode/Geode.hpp>

namespace dashcore::renderer::geode {
namespace {
RendererCoordinator g_coordinator;
}

RendererCoordinator& coordinator() noexcept {
    return g_coordinator;
}

} // namespace dashcore::renderer::geode

namespace dashcore::renderer {

void initialize() {
    ::geode::log::info(
        "DashCore 0.13.0-alpha: original Geometry Dash/Geode Cocos rendering is retained; memory diagnostics and both GLES compute probes are opt-in."
    );
}

} // namespace dashcore::renderer
