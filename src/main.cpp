#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>
#include <cstdint>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    // Lightweight, one-shot profiler for the first 180 gameplay updates.
    // It observes CPU time spent inside PlayLayer::update; it does not alter
    // object creation, visibility, rendering, physics, or frame scheduling.
    struct StartupProfile {
        PlayLayer* layer = nullptr;
        std::uint32_t frames = 0;
        std::uint32_t slowUpdates = 0;
        std::int64_t totalUpdateUs = 0;
        std::int64_t maxUpdateUs = 0;
        Clock::time_point previousFrame{};
        std::int64_t maxFrameGapUs = 0;
        std::uint32_t longFrameGaps = 0;
    };

    StartupProfile g_profile;
    constexpr std::uint32_t kFramesToMeasure = 180;
    constexpr std::int64_t kSlowUpdateUs = 8000;
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
            g_profile.layer = this;
            g_profile.previousFrame = {};
            log::info(
                "OptGod: PlayLayer init took {} ms (level id: {}). Starting first-180-frame profile.",
                elapsedUs / 1000,
                level ? level->m_levelID : 0
            );
        } else {
            log::warn("OptGod: PlayLayer init failed after {} ms.", elapsedUs / 1000);
        }

        return result;
    }

    void update(float dt) {
        bool const profiling = g_profile.layer == this &&
            g_profile.frames < kFramesToMeasure;

        auto const frameStart = Clock::now();
        if (profiling && g_profile.previousFrame.time_since_epoch().count() != 0) {
            auto const gapUs = std::chrono::duration_cast<std::chrono::microseconds>(
                frameStart - g_profile.previousFrame
            ).count();
            if (gapUs > g_profile.maxFrameGapUs) {
                g_profile.maxFrameGapUs = gapUs;
            }
            if (gapUs > kLongFrameGapUs) {
                ++g_profile.longFrameGaps;
            }
        }

        PlayLayer::update(dt);

        if (!profiling) {
            return;
        }

        auto const updateUs = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - frameStart
        ).count();

        g_profile.previousFrame = frameStart;
        ++g_profile.frames;
        g_profile.totalUpdateUs += updateUs;
        if (updateUs > g_profile.maxUpdateUs) {
            g_profile.maxUpdateUs = updateUs;
        }
        if (updateUs > kSlowUpdateUs) {
            ++g_profile.slowUpdates;
        }

        if (g_profile.frames == kFramesToMeasure) {
            auto const averageUs = g_profile.totalUpdateUs / kFramesToMeasure;
            log::info(
                "OptGod startup profile ({} frames): update avg={} us, max={} us, updates >8ms={}, frame gaps >25ms={}, max frame gap={} us.",
                g_profile.frames,
                averageUs,
                g_profile.maxUpdateUs,
                g_profile.slowUpdates,
                g_profile.longFrameGaps,
                g_profile.maxFrameGapUs
            );
            g_profile.layer = nullptr;
        }
    }
};

$on_mod(Loaded) {
    log::info(
        "OptGod loaded: lightweight startup profiling enabled; gameplay and rendering are unchanged."
    );
}
