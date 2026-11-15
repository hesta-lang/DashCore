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
        "DashCore 0.16.1-alpha: original Geometry Dash/Geode Cocos rendering is retained; synthetic CPU object grouping, memory diagnostics, and all GLES compute/SSBO probes are opt-in; the original Cocos renderer remains the only visible draw path."
    );
}

} // namespace dashcore::renderer
