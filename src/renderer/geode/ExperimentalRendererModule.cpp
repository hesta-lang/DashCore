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
        "DashCore Bismuth Bridge 0.11.7-alpha: the optional CPU trigger-state mirror is available; the original Geometry Dash renderer remains the only draw path."
    );
}

} // namespace dashcore::renderer
