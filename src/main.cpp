#include <Geode/Geode.hpp>
#include "renderer/geode/ExperimentalRendererModule.hpp"

$on_mod(Loaded) {
    dashcore::renderer::initialize();
}
