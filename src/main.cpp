#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

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
        std::uint32_t visibilityFrames = 0;
        std::uint32_t slowVisibilityUpdates = 0;
        std::int64_t totalVisibilityUs = 0;
        std::int64_t maxVisibilityUs = 0;
        std::uint64_t visitedObjects = 0;
        std::uint64_t eligibleObjects = 0;
        std::uint64_t protectedEffectObjects = 0;
        std::uint64_t protectedGroupedObjects = 0;
        std::uint64_t boundsChecks = 0;
        std::uint64_t culledObjects = 0;
        std::uint64_t sceneNodes = 0;
        std::uint64_t sceneGameObjects = 0;
        std::uint64_t sceneEffectObjects = 0;
        std::uint64_t sceneGroupedObjects = 0;
        std::uint64_t sceneHiddenGameObjects = 0;
        std::uint32_t sceneMaxChildren = 0;
        std::uint32_t sceneMaxDepth = 0;
        std::uint32_t sceneScans = 0;
        std::int64_t sceneScanUs = 0;
        bool dontCreateObjects = false;
    };

    StartupProfile g_profile;
    bool g_cullingEnabled = false;
    bool g_cullingRequested = false;
    bool g_lastDontCreateObjects = false;

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
    struct SceneScan {
        std::uint64_t nodes = 0;
        std::uint64_t gameObjects = 0;
        std::uint64_t effectObjects = 0;
        std::uint64_t groupedObjects = 0;
        std::uint64_t hiddenGameObjects = 0;
        std::uint32_t maxChildren = 0;
        std::uint32_t maxDepth = 0;
    };

    // Read-only scene-graph inspection. This deliberately does not change
    // visibility, transforms, scheduling, or object state.
    SceneScan inspectSceneGraph(CCNode* root) {
        SceneScan result;
        if (!root) return result;

        std::vector<std::pair<CCNode*, std::uint32_t>> pending;
        pending.emplace_back(root, 0);

        while (!pending.empty()) {
            auto const entry = pending.back();
            pending.pop_back();
            auto* node = entry.first;
            auto const depth = entry.second;
            if (!node) continue;

            ++result.nodes;
            result.maxDepth = std::max(result.maxDepth, depth);

            if (auto* object = typeinfo_cast<GameObject*>(node)) {
                ++result.gameObjects;
                if (object->m_classType == GameObjectClassType::Effect) {
                    ++result.effectObjects;
                }
                if (object->m_groupCount > 0) {
                    ++result.groupedObjects;
                }
                if (!object->isVisible()) {
                    ++result.hiddenGameObjects;
                }
            }

            auto const childCount = node->getChildrenCount();
            result.maxChildren = std::max(
                result.maxChildren, static_cast<std::uint32_t>(childCount)
            );
            if (childCount == 0 || !node->getChildren()) continue;

            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (child) pending.emplace_back(child, depth + 1);
            }
        }
        return result;
    }

    void reportSceneGraph(char const* stage) {
        log::info(
            "DashCore 0.6.1 SCENE-GRAPH {}: scans={}, nodes={}, gameObjects={}, effects={}, grouped={}, hiddenGameObjects={}, maxChildren={}, maxDepth={}, lastScan={}us, dontCreateObjects={}.",
            stage, g_profile.sceneScans, g_profile.sceneNodes,
            g_profile.sceneGameObjects, g_profile.sceneEffectObjects,
            g_profile.sceneGroupedObjects, g_profile.sceneHiddenGameObjects,
            g_profile.sceneMaxChildren, g_profile.sceneMaxDepth,
            g_profile.sceneScanUs, g_profile.dontCreateObjects
        );
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
            "DashCore 0.6.0 {} buckets: <=2ms={}, 2-4ms={}, 4-8ms={}, 8-16ms={}, 16-25ms={}, >25ms={}.",
            label, b[0], b[1], b[2], b[3], b[4], b[5]
        );
    }

    void logGapBuckets(char const* label, Buckets const& b) {
        log::info(
            "DashCore 0.6.0 {} buckets: <=16.67ms={}, 16.67-25ms={}, 25-50ms={}, 50-100ms={}, >100ms={}.",
            label, b[0], b[1], b[2], b[3], b[4]
        );
    }

    void maybeFinishProfile() {
        if (g_profile.updateFrames >= kFramesToMeasure &&
            g_profile.drawFrames >= kFramesToMeasure) {
            log::info("DashCore 0.6.0: both 180-frame samples complete; diagnostic-only.");
            g_profile.layer = nullptr;
        }
    }

    void reportUpdateProfile(char const* stage) {
        if (g_profile.updateFrames == 0) return;
        log::info(
            "DashCore 0.6.0 UPDATE {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
            stage, g_profile.updateFrames,
            g_profile.totalPostUpdateUs / g_profile.updateFrames,
            g_profile.maxPostUpdateUs, g_profile.slowPostUpdates,
            g_profile.longUpdateGaps, g_profile.maxUpdateGapUs,
            g_profile.minDt, g_profile.maxDt
        );
        logBuckets("UPDATE duration", g_profile.updateDurationBuckets);
        logGapBuckets("UPDATE frame-gap", g_profile.updateGapBuckets);
    }

    void reportVisibilityProfile(char const* stage) {
        if (g_profile.visibilityFrames == 0) return;
        log::info(
            "DashCore 0.6.1 VISIBILITY {}: frames={}, avg={}us, max={}us, over8ms={}.",
            stage, g_profile.visibilityFrames,
            g_profile.totalVisibilityUs / g_profile.visibilityFrames,
            g_profile.maxVisibilityUs, g_profile.slowVisibilityUpdates
        );
    }

    void reportCullStats(char const* stage) {
        log::info(
            "DashCore 0.6.0 CULL {}: requested={}, active={}, frames={}, visited={}, eligible={}, protectedEffect={}, protectedGrouped={}, boundsChecks={}, culled={}.",
            stage, g_cullingRequested, g_cullingEnabled, g_profile.drawFrames,
            g_profile.visitedObjects, g_profile.eligibleObjects,
            g_profile.protectedEffectObjects, g_profile.protectedGroupedObjects,
            g_profile.boundsChecks, g_profile.culledObjects
        );
    }

    void reportDrawProfile(char const* stage) {
        if (g_profile.drawFrames == 0) return;
        log::info(
            "DashCore 0.6.0 DRAWSCENE-CPU {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us. GPU execution time is not measured.",
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

// GameObject inherits the Cocos visit implementation instead of providing
// its own hookable visit method. Hook CCNode::visit and identify GameObjects
// with Geode's runtime type information before applying the culling test.
class $modify(OptGodCCNode, cocos2d::CCNode) {
    void visit() {
        // Avoid RTTI and bounds work entirely when experimental culling is off.
        if (!g_cullingEnabled) {
            CCNode::visit();
            return;
        }
        auto* object = typeinfo_cast<GameObject*>(this);
        bool const profiling = object && g_profile.layer != nullptr &&
            g_profile.drawFrames < kFramesToMeasure;

        bool const isEffect = object &&
            object->m_classType == GameObjectClassType::Effect;
        bool const isGrouped = object && object->m_groupCount > 0;
        bool const eligible = object && !isEffect && !isGrouped;

        if (object && profiling) {
            ++g_profile.visitedObjects;
            if (isEffect) {
                ++g_profile.protectedEffectObjects;
            } else if (isGrouped) {
                ++g_profile.protectedGroupedObjects;
            } else {
                ++g_profile.eligibleObjects;
                ++g_profile.boundsChecks;
            }
        }

        // Skip only the visual visit of distant, ungrouped, non-Effect
        // GameObjects. Their update, collision and trigger logic is untouched.
        if (g_cullingEnabled && eligible && shouldCullStaticObject(object)) {
            if (profiling) ++g_profile.culledObjects;
            return;
        }

        CCNode::visit();
    }
};

// Profile the engine's own visibility pass. This helps identify whether
// recurring frame spikes come from visibility management rather than drawing.
class $modify(OptGodBaseGameLayer, GJBaseGameLayer) {
    void updateVisibility(float dt) {
        bool const profiling = g_profile.layer != nullptr &&
            g_profile.visibilityFrames < kFramesToMeasure;
        auto const start = Clock::now();
        GJBaseGameLayer::updateVisibility(dt);
        if (!profiling) return;

        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());
        ++g_profile.visibilityFrames;
        g_profile.totalVisibilityUs += elapsedUs;
        g_profile.maxVisibilityUs = std::max(g_profile.maxVisibilityUs, elapsedUs);
        if (elapsedUs > kSlowCallbackUs) ++g_profile.slowVisibilityUpdates;
        if (g_profile.visibilityFrames % kReportEvery == 0 ||
            g_profile.visibilityFrames == kFramesToMeasure) {
            reportVisibilityProfile(
                g_profile.visibilityFrames == kFramesToMeasure ? "FINAL" : "progress"
            );
        }
    }
};

class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_profile = {};
        g_lastDontCreateObjects = dontCreateObjects;

        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        if (result) {
            log::info(
                "DashCore 0.6.0: PlayLayer::init={}us ({}ms), levelID={}, replay={}, dontCreateObjects={}.",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("DashCore 0.6.0: PlayLayer::init failed after {}us.", elapsedUs);
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
        g_cullingRequested = Mod::get()->getSettingValue<bool>("experimental-render-culling");
        // The experimental option controls the live CCNode::visit culling.
        // It is intentionally user-toggleable and defaults on in 0.6.0.
        g_cullingEnabled = g_cullingRequested;
        g_profile.dontCreateObjects = g_lastDontCreateObjects;
        log::info(
            "DashCore 0.6.0: setupHasCompleted={}us ({}ms); culling-requested={}, culling-active={}, dontCreateObjects={}; collecting frame, visit-hook, and read-only scene-graph diagnostics.",
            elapsedUs, elapsedUs / 1000, g_cullingRequested, g_cullingEnabled,
            g_profile.dontCreateObjects
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

        if (profiling && (g_profile.drawFrames % kReportEvery == 0)) {
            auto const scanStart = Clock::now();
            auto const scan = inspectSceneGraph(g_profile.layer);
            g_profile.sceneScanUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - scanStart).count());
            ++g_profile.sceneScans;
            g_profile.sceneNodes = scan.nodes;
            g_profile.sceneGameObjects = scan.gameObjects;
            g_profile.sceneEffectObjects = scan.effectObjects;
            g_profile.sceneGroupedObjects = scan.groupedObjects;
            g_profile.sceneHiddenGameObjects = scan.hiddenGameObjects;
            g_profile.sceneMaxChildren = scan.maxChildren;
            g_profile.sceneMaxDepth = scan.maxDepth;
            reportSceneGraph("snapshot");
        }

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
            reportSceneGraph("FINAL");
        }
        maybeFinishProfile();
    }
};

$on_mod(Loaded) {
    log::info(
        "DashCore 0.6.1 loaded: experimental render culling is opt-in; visibility-pass profiling is enabled."
    );
}
