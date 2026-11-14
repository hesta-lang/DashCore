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
        "DashCore Bismuth Bridge 0.11.6-alpha: read-only Cocos object, sprite and batch adapters are available; the original Geometry Dash renderer remains the only draw path."
    );
}

} // namespace dashcore::renderer
