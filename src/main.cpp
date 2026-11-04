#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    struct StartupProfile {
        PlayLayer* layer = nullptr;
        std::uint32_t updateFrames = 0;
        std::uint32_t drawFrames = 0;
        std::uint32_t slowPostUpdates = 0;
        std::uint32_t slowDrawScenes = 0;
        std::uint32_t longUpdateGaps = 0;
        std::uint32_t longDrawGaps = 0;
        std::int64_t totalPostUpdateUs = 0;
        std::int64_t maxPostUpdateUs = 0;
        std::int64_t totalDrawSceneUs = 0;
        std::int64_t maxDrawSceneUs = 0;
        std::int64_t maxUpdateGapUs = 0;
        std::int64_t maxDrawGapUs = 0;
        float minDt = std::numeric_limits<float>::max();
        float maxDt = 0.f;
        Clock::time_point previousPostUpdate{};
        Clock::time_point previousDrawScene{};
    };

    StartupProfile g_profile;
    constexpr std::uint32_t kFramesToMeasure = 180;
    constexpr std::uint32_t kReportEvery = 30;
    constexpr std::int64_t kSlowCallbackUs = 8000;
    constexpr std::int64_t kLongFrameGapUs = 25000;

    void maybeFinishProfile() {
        if (g_profile.updateFrames >= kFramesToMeasure &&
            g_profile.drawFrames >= kFramesToMeasure) {
            log::info("OptGod 0.3.0: both 180-frame samples complete; diagnostic-only.");
            g_profile.layer = nullptr;
        }
    }

    void reportUpdateProfile(char const* stage) {
        if (g_profile.updateFrames == 0) return;
        log::info(
            "OptGod 0.3.0 UPDATE {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
            stage, g_profile.updateFrames,
            g_profile.totalPostUpdateUs / g_profile.updateFrames,
            g_profile.maxPostUpdateUs, g_profile.slowPostUpdates,
            g_profile.longUpdateGaps, g_profile.maxUpdateGapUs,
            g_profile.minDt, g_profile.maxDt
        );
    }

    void reportDrawProfile(char const* stage) {
        if (g_profile.drawFrames == 0) return;
        log::info(
            "OptGod 0.3.0 DRAWSCENE-CPU {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us. GPU execution time is not measured.",
            stage, g_profile.drawFrames,
            g_profile.totalDrawSceneUs / g_profile.drawFrames,
            g_profile.maxDrawSceneUs, g_profile.slowDrawScenes,
            g_profile.longDrawGaps, g_profile.maxDrawGapUs
        );
    }
}

class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_profile = {};

        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        if (result) {
            log::info(
                "OptGod 0.3.0: PlayLayer::init={}us ({}ms), levelID={}, replay={}, dontCreateObjects={}.",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("OptGod 0.3.0: PlayLayer::init failed after {}us.", elapsedUs);
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
            "OptGod 0.3.0: setupHasCompleted={}us ({}ms); collecting 180 UPDATE and 180 DRAWSCENE samples.",
            elapsedUs, elapsedUs / 1000
        );
    }

    void postUpdate(float dt) {
        bool const profiling = g_profile.layer == this &&
            g_profile.updateFrames < kFramesToMeasure;
        auto const start = Clock::now();

        if (profiling && g_profile.previousPostUpdate.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    start - g_profile.previousPostUpdate).count());
            g_profile.maxUpdateGapUs = std::max(g_profile.maxUpdateGapUs, gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.longUpdateGaps;
        }

        PlayLayer::postUpdate(dt);
        if (!profiling) return;

        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        g_profile.previousPostUpdate = start;
        ++g_profile.updateFrames;
        g_profile.totalPostUpdateUs += elapsedUs;
        g_profile.maxPostUpdateUs = std::max(g_profile.maxPostUpdateUs, elapsedUs);
        g_profile.minDt = std::min(g_profile.minDt, dt);
        g_profile.maxDt = std::max(g_profile.maxDt, dt);
        if (elapsedUs > kSlowCallbackUs) ++g_profile.slowPostUpdates;

        if (g_profile.updateFrames % kReportEvery == 0) {
            reportUpdateProfile("progress");
        }
        if (g_profile.updateFrames == kFramesToMeasure) {
            reportUpdateProfile("FINAL");
        }
        maybeFinishProfile();
    }
};

class $modify(OptGodDirector, cocos2d::CCDirector) {
    void drawScene() {
        bool const profiling = g_profile.layer != nullptr &&
            g_profile.drawFrames < kFramesToMeasure;
        auto const start = Clock::now();

        if (profiling && g_profile.previousDrawScene.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    start - g_profile.previousDrawScene).count());
            g_profile.maxDrawGapUs = std::max(g_profile.maxDrawGapUs, gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.longDrawGaps;
        }

        CCDirector::drawScene();
        if (!profiling) return;

        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        g_profile.previousDrawScene = start;
        ++g_profile.drawFrames;
        g_profile.totalDrawSceneUs += elapsedUs;
        g_profile.maxDrawSceneUs = std::max(g_profile.maxDrawSceneUs, elapsedUs);
        if (elapsedUs > kSlowCallbackUs) ++g_profile.slowDrawScenes;

        if (g_profile.drawFrames % kReportEvery == 0) {
            reportDrawProfile("progress");
        }
        if (g_profile.drawFrames == kFramesToMeasure) {
            reportDrawProfile("FINAL");
        }
        maybeFinishProfile();
    }
};

$on_mod(Loaded) {
    log::info(
        "OptGod 0.3.0 loaded: measures PlayLayer init/setup/postUpdate and CPU time in CCDirector::drawScene for 180 frames. No gameplay or rendering behavior is changed."
    );
}
