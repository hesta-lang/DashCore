#include <Geode/Geode.hpp>
#include <chrono>

using namespace geode::prelude;

// First safe step: measure PlayLayer construction time on-device.
// This does not change gameplay, rendering, object visibility, or scheduling.
class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto const start = std::chrono::steady_clock::now();

        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);

        auto const end = std::chrono::steady_clock::now();
        auto const elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (result) {
            log::info(
                "OptGod: PlayLayer initialization completed in {} ms (level id: {}).",
                elapsedMs,
                level ? level->m_levelID : 0
            );
        } else {
            log::warn("OptGod: PlayLayer initialization failed after {} ms.", elapsedMs);
        }

        return result;
    }
};

$on_mod(Loaded) {
    log::info("OptGod loaded: level-load profiling enabled; gameplay and rendering remain unchanged.");
}
