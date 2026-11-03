#include <Geode/Geode.hpp>

using namespace geode::prelude;

// OptGod starts deliberately passive. Rendering changes will only be enabled
// after their visibility bounds and gameplay-safety behavior are validated.
$on_mod(Loaded) {
    log::info("OptGod loaded: passive baseline build (no rendering changes enabled).");
}
