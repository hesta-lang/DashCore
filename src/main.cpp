#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

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
        float minDt = std::numeric_limits<float>::max();
        float maxDt = 0.f;
        Clock::time_point previousPostUpdate{};
    };

    StartupProfile g_profile;
    constexpr std::uint32_t kFramesToMeasure = 180;
    constexpr std::uint32_t kReportEvery = 30;
    constexpr std::int64_t kSlowPostUpdateUs = 8000;
    constexpr std::int64_t kLongFrameGapUs = 25000;

    void reportProfile(char const* stage) {
        if (g_profile.frames == 0) return;
        auto const avgUs = g_profile.totalPostUpdateUs / g_profile.frames;
        log::info(
            "OptGod REVIEW {}: frames={}, avgPostUpdate={}us, maxPostUpdate={}us, "
            "postUpdates>8ms={}, gaps>25ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
            stage, g_profile.frames, avgUs, g_profile.maxPostUpdateUs,
            g_profile.slowPostUpdates, g_profile.longFrameGaps,
            g_profile.maxFrameGapUs, g_profile.minDt, g_profile.maxDt
        );
    }
}

class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        // Clear old samples BEFORE the original init. setupHasCompleted may be
        // called from inside PlayLayer::init and assigns g_profile.layer.
        g_profile = {};

        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        if (result) {
            log::info(
                "OptGod REVIEW: PlayLayer::init={}us ({}ms), levelID={}, replay={}, dontCreateObjects={}.",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("OptGod REVIEW: PlayLayer::init failed after {}us.", elapsedUs);
        }
        return result;
    }

    void setupHasCompleted() {
        auto const start = Clock::now();
        PlayLayer::setupHasCompleted();
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        g_profile = {};
        g_profile.layer = this;
        log::info(
            "OptGod REVIEW: setupHasCompleted={}us ({}ms); waiting for {} PlayLayer::postUpdate calls.",
            elapsedUs, elapsedUs / 1000, kFramesToMeasure
        );
    }

    void postUpdate(float dt) {
        bool const profiling = g_profile.layer == this &&
            g_profile.frames < kFramesToMeasure;
        auto const postUpdateStart = Clock::now();

        if (profiling && g_profile.previousPostUpdate.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    postUpdateStart - g_profile.previousPostUpdate).count());
            g_profile.maxFrameGapUs = std::max(g_profile.maxFrameGapUs, gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.longFrameGaps;
        }

        PlayLayer::postUpdate(dt);

        if (!profiling) return;

        auto const postUpdateUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - postUpdateStart).count());

        g_profile.previousPostUpdate = postUpdateStart;
        ++g_profile.frames;
        g_profile.totalPostUpdateUs += postUpdateUs;
        g_profile.maxPostUpdateUs = std::max(g_profile.maxPostUpdateUs, postUpdateUs);
        g_profile.minDt = std::min(g_profile.minDt, dt);
        g_profile.maxDt = std::max(g_profile.maxDt, dt);
        if (postUpdateUs > kSlowPostUpdateUs) ++g_profile.slowPostUpdates;

        if (g_profile.frames == 1) {
            log::info("OptGod REVIEW: first PlayLayer::postUpdate reached (dt={:.4f}s).", dt);
        }

        if (g_profile.frames % kReportEvery == 0) {
            reportProfile("progress");
        }

        if (g_profile.frames == kFramesToMeasure) {
            reportProfile("FINAL");
            log::info(
                "OptGod REVIEW: postUpdate sample complete; diagnostic-only, "
                "no gameplay/rendering behavior was changed."
            );
            g_profile.layer = nullptr;
        }
    }
};

$on_mod(Loaded) {
    log::info(
        "OptGod REVIEW build loaded: measures PlayLayer init, setupHasCompleted "
        "and 180 PlayLayer::postUpdate calls. No optimization or visual/gameplay changes are active."
    );
}
