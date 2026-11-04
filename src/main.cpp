#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>
#include <cstdint>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    struct StartupProfile {
        PlayLayer* layer = nullptr;
        std::uint32_t frames = 0;
        std::uint32_t slowUpdates = 0;
        std::uint32_t longFrameGaps = 0;
        std::int64_t totalUpdateUs = 0;
        std::int64_t maxUpdateUs = 0;
        std::int64_t maxFrameGapUs = 0;
        Clock::time_point previousUpdate{};
    };

    StartupProfile g_profile;
    constexpr std::uint32_t kFramesToMeasure = 120;
    constexpr std::int64_t kSlowUpdateUs = 8000;
    constexpr std::int64_t kLongFrameGapUs = 25000;
}

class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - start).count();

        if (result) {
            g_profile = {};
            log::info("OptGod: PlayLayer init took {} ms (level id: {}).",
                elapsedUs / 1000, level ? level->m_levelID : 0);
        } else {
            log::warn("OptGod: PlayLayer init failed after {} ms.", elapsedUs / 1000);
        }
        return result;
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        g_profile = {};
        g_profile.layer = this;
        log::info("OptGod: level setup completed; profiling next {} update calls.", kFramesToMeasure);
    }

    void update(float dt) {
        bool const profiling = g_profile.layer == this &&
            g_profile.frames < kFramesToMeasure;
        auto const updateStart = Clock::now();

        if (profiling && g_profile.previousUpdate.time_since_epoch().count() != 0) {
            auto const gapUs = std::chrono::duration_cast<std::chrono::microseconds>(
                updateStart - g_profile.previousUpdate).count();
            if (gapUs > g_profile.maxFrameGapUs) g_profile.maxFrameGapUs = gapUs;
            if (gapUs > kLongFrameGapUs) ++g_profile.longFrameGaps;
        }

        PlayLayer::update(dt);

        if (!profiling) return;

        auto const updateUs = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - updateStart).count();
        g_profile.previousUpdate = updateStart;
        ++g_profile.frames;
        g_profile.totalUpdateUs += updateUs;
        if (updateUs > g_profile.maxUpdateUs) g_profile.maxUpdateUs = updateUs;
        if (updateUs > kSlowUpdateUs) ++g_profile.slowUpdates;

        if (g_profile.frames == kFramesToMeasure) {
            log::info("OptGod startup profile ({} update calls): avg={} us, max={} us, updates >8ms={}, gaps >25ms={}, max gap={} us.",
                g_profile.frames, g_profile.totalUpdateUs / kFramesToMeasure,
                g_profile.maxUpdateUs, g_profile.slowUpdates,
                g_profile.longFrameGaps, g_profile.maxFrameGapUs);
            g_profile.layer = nullptr;
        }
    }
};

$on_mod(Loaded) {
    log::info("OptGod loaded: level setup and main update profiling enabled; no gameplay or rendering changes.");
}
