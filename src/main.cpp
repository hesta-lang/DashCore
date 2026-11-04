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
        std::uint32_t slowPostUpdates = 0;
        std::uint32_t longFrameGaps = 0;
        std::int64_t totalPostUpdateUs = 0;
        std::int64_t maxPostUpdateUs = 0;
        std::int64_t maxFrameGapUs = 0;
        Clock::time_point previousPostUpdate{};
    };

    StartupProfile g_profile;
    constexpr std::uint32_t kFramesToMeasure = 120;
    constexpr std::int64_t kSlowPostUpdateUs = 8000;
    constexpr std::int64_t kLongFrameGapUs = 25000;
}

class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();

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
        log::info("OptGod: level setup completed; measuring next {} post-update frames.", kFramesToMeasure);
    }

    void postUpdate(float dt) {
        bool const profiling = g_profile.layer == this && g_profile.frames < kFramesToMeasure;
        auto const frameStart = Clock::now();

        if (profiling && g_profile.previousPostUpdate.time_since_epoch().count() != 0) {
            auto const gapUs = std::chrono::duration_cast<std::chrono::microseconds>(
                frameStart - g_profile.previousPostUpdate).count();
            if (gapUs > g_profile.maxFrameGapUs) g_profile.maxFrameGapUs = gapUs;
            if (gapUs > kLongFrameGapUs) ++g_profile.longFrameGaps;
        }

        PlayLayer::postUpdate(dt);

        if (!profiling) return;

        auto const postUpdateUs = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - frameStart).count();
        g_profile.previousPostUpdate = frameStart;
        ++g_profile.frames;
        g_profile.totalPostUpdateUs += postUpdateUs;
        if (postUpdateUs > g_profile.maxPostUpdateUs) g_profile.maxPostUpdateUs = postUpdateUs;
        if (postUpdateUs > kSlowPostUpdateUs) ++g_profile.slowPostUpdates;

        if (g_profile.frames == kFramesToMeasure) {
            log::info("OptGod startup profile ({} post-update frames): avg={} us, max={} us, post-updates >8ms={}, frame gaps >25ms={}, max frame gap={} us.",
                g_profile.frames, g_profile.totalPostUpdateUs / kFramesToMeasure,
                g_profile.maxPostUpdateUs, g_profile.slowPostUpdates,
                g_profile.longFrameGaps, g_profile.maxFrameGapUs);
            g_profile.layer = nullptr;
        }
    }
};

$on_mod(Loaded) {
    log::info("OptGod loaded: level setup and first-frame profiling enabled; no gameplay or rendering changes.");
}
