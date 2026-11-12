#include "renderer/geode/ExperimentalRendererModule.hpp"

#include <Geode/Geode.hpp>

namespace dashcore::renderer {

void initialize() {
    geode::log::info(
        "DashCore Experimental Renderer 0.11.0-alpha: read-only pipeline foundation loaded; vanilla rendering remains active."
    );
}

} // namespace dashcore::renderer
