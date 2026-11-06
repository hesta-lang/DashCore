#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GameObject.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    // Buckets are <=2, <=4, <=8, <=16, <=25, and >25 milliseconds.
    using Buckets = std::array<std::uint32_t, 6>;

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
        Buckets updateDurationBuckets{};
        Buckets drawDurationBuckets{};
        Buckets updateGapBuckets{};
        Buckets drawGapBuckets{};
        Clock::time_point previousPostUpdate{};
        Clock::time_point previousDrawScene{};
        std::uint64_t visitedObjects = 0;
        std::uint64_t eligibleObjects = 0;
        std::uint64_t protectedEffectObjects = 0;
        std::uint64_t protectedGroupedObjects = 0;
        std::uint64_t boundsChecks = 0;
        std::uint64_t culledObjects = 0;
    };

    StartupProfile g_profile;
    bool g_cullingEnabled = false;

    // First experimental stage: skip only the draw traversal of distant,
    // static, ungrouped GameObjects. Effect objects (including triggers) and
    // grouped objects are deliberately never culled.
    bool shouldCullStaticObject(GameObject* object) {
        if (!object || object->m_classType == GameObjectClassType::Effect ||
            object->m_groupCount > 0) {
            return false;
        }

        auto parent = object->getParent();
        if (!parent) return false;

        auto bounds = object->getBoundingRect();
        auto const x1 = bounds.origin.x;
        auto const y1 = bounds.origin.y;
        auto const x2 = x1 + bounds.size.width;
        auto const y2 = y1 + bounds.size.height;

        auto const p1 = parent->convertToWorldSpace({ x1, y1 });
        auto const p2 = parent->convertToWorldSpace({ x2, y1 });
        auto const p3 = parent->convertToWorldSpace({ x1, y2 });
        auto const p4 = parent->convertToWorldSpace({ x2, y2 });

        auto const minX = std::min({ p1.x, p2.x, p3.x, p4.x });
        auto const maxX = std::max({ p1.x, p2.x, p3.x, p4.x });
        auto const minY = std::min({ p1.y, p2.y, p3.y, p4.y });
        auto const maxY = std::max({ p1.y, p2.y, p3.y, p4.y });

        auto const screen = cocos2d::CCDirector::sharedDirector()->getWinSize();
        // Keep a generous guard band so nearby off-screen objects are already
        // drawn before camera movement brings them into view.
        auto const marginX = screen.width * 0.75f;
        auto const marginY = screen.height * 0.75f;

        return maxX < -marginX || minX > screen.width + marginX ||
               maxY < -marginY || minY > screen.height + marginY;
    }
    constexpr std::uint32_t kFramesToMeasure = 180;
    constexpr std::uint32_t kReportEvery = 30;
    constexpr std::int64_t kSlowCallbackUs = 8000;
    constexpr std::int64_t kLongFrameGapUs = 25000;

    std::size_t durationBucket(std::int64_t us) {
        if (us <= 2000) return 0;
        if (us <= 4000) return 1;
        if (us <= 8000) return 2;
        if (us <= 16000) return 3;
        if (us <= 25000) return 4;
        return 5;
    }

    std::size_t gapBucket(std::int64_t us) {
        if (us <= 16667) return 0;
        if (us <= 25000) return 1;
        if (us <= 50000) return 2;
        if (us <= 100000) return 3;
        return 4;
    }

    void addGap(Buckets& buckets, std::int64_t us) {
        auto const bucket = gapBucket(us);
        if (bucket < buckets.size()) ++buckets[bucket];
    }

    void addDuration(Buckets& buckets, std::int64_t us) {
        ++buckets[durationBucket(us)];
    }

    void logBuckets(char const* label, Buckets const& b) {
        log::info(
            "OptGod 0.5.1 {} buckets: <=2ms={}, 2-4ms={}, 4-8ms={}, 8-16ms={}, 16-25ms={}, >25ms={}.",
            label, b[0], b[1], b[2], b[3], b[4], b[5]
        );
    }

    void logGapBuckets(char const* label, Buckets const& b) {
        log::info(
            "OptGod 0.5.1 {} buckets: <=16.67ms={}, 16.67-25ms={}, 25-50ms={}, 50-100ms={}, >100ms={}.",
            label, b[0], b[1], b[2], b[3], b[4]
        );
    }

    void maybeFinishProfile() {
        if (g_profile.updateFrames >= kFramesToMeasure &&
            g_profile.drawFrames >= kFramesToMeasure) {
            log::info("OptGod 0.5.1: both 180-frame samples complete; diagnostic-only.");
            g_profile.layer = nullptr;
        }
    }

    void reportUpdateProfile(char const* stage) {
        if (g_profile.updateFrames == 0) return;
        log::info(
            "OptGod 0.5.1 UPDATE {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
            stage, g_profile.updateFrames,
            g_profile.totalPostUpdateUs / g_profile.updateFrames,
            g_profile.maxPostUpdateUs, g_profile.slowPostUpdates,
            g_profile.longUpdateGaps, g_profile.maxUpdateGapUs,
            g_profile.minDt, g_profile.maxDt
        );
        logBuckets("UPDATE duration", g_profile.updateDurationBuckets);
        logGapBuckets("UPDATE frame-gap", g_profile.updateGapBuckets);
    }

    void reportCullStats(char const* stage) {
        log::info(
            "OptGod 0.5.1 CULL {}: enabled={}, frames={}, visited={}, eligible={}, protectedEffect={}, protectedGrouped={}, boundsChecks={}, culled={}.",
            stage, g_cullingEnabled, g_profile.drawFrames,
            g_profile.visitedObjects, g_profile.eligibleObjects,
            g_profile.protectedEffectObjects, g_profile.protectedGroupedObjects,
            g_profile.boundsChecks, g_profile.culledObjects
        );
    }

    void reportDrawProfile(char const* stage) {
        if (g_profile.drawFrames == 0) return;
        log::info(
            "OptGod 0.5.1 DRAWSCENE-CPU {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us. GPU execution time is not measured.",
            stage, g_profile.drawFrames,
            g_profile.totalDrawSceneUs / g_profile.drawFrames,
            g_profile.maxDrawSceneUs, g_profile.slowDrawScenes,
            g_profile.longDrawGaps, g_profile.maxDrawGapUs
        );
        logBuckets("DRAWSCENE duration", g_profile.drawDurationBuckets);
        reportCullStats(stage);
        logGapBuckets("DRAWSCENE frame-gap", g_profile.drawGapBuckets);
    }
}

class $modify(OptGodGameObject, GameObject) {
    void visit() {
        bool const profiling = g_profile.layer != nullptr &&
            g_profile.drawFrames < kFramesToMeasure;

        if (profiling) {
            ++g_profile.visitedObjects;
            if (m_classType == GameObjectClassType::Effect) {
                ++g_profile.protectedEffectObjects;
            } else if (m_groupCount > 0) {
                ++g_profile.protectedGroupedObjects;
            } else {
                ++g_profile.eligibleObjects;
            }
        }

        if (g_cullingEnabled &&
            m_classType != GameObjectClassType::Effect &&
            m_groupCount <= 0) {
            if (profiling) ++g_profile.boundsChecks;
            if (shouldCullStaticObject(this)) {
                if (profiling) ++g_profile.culledObjects;
                return;
            }
        }

        GameObject::visit();
    }
};

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
                "OptGod 0.5.1: PlayLayer::init={}us ({}ms), levelID={}, replay={}, dontCreateObjects={}.",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("OptGod 0.5.1: PlayLayer::init failed after {}us.", elapsedUs);
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
        g_cullingEnabled = Mod::get()->getSettingValue<bool>("experimental-render-culling");
        log::info(
            "OptGod 0.5.1: setupHasCompleted={}us ({}ms); experimental-render-culling={}; collecting 180 UPDATE and 180 DRAWSCENE samples plus object-culling counters.",
            elapsedUs, elapsedUs / 1000, g_cullingEnabled
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
            addGap(g_profile.updateGapBuckets, gapUs);
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
        addDuration(g_profile.updateDurationBuckets, elapsedUs);
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
            addGap(g_profile.drawGapBuckets, gapUs);
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
        addDuration(g_profile.drawDurationBuckets, elapsedUs);
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
        "OptGod 0.5.1 loaded: Android64 profiler plus instrumented opt-in experimental render culling. Object visit, eligible, protected, bounds-check and culled counters are collected during the first 180 drawScene samples; Effect and grouped objects remain protected."
    );
}
