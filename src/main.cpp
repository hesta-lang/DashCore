#include <Geode/Geode.hpp>
#include "bismuth/GLESComputeProbe.hpp"
#include "bismuth/GLESObjectCullingProbe.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCNode.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
        std::uint64_t visitedObjects = 0;
        std::uint64_t eligibleObjects = 0;
        std::uint64_t protectedEffectObjects = 0;
        std::uint64_t protectedTriggerObjects = 0;
        std::uint64_t protectedGroupedNearObjects = 0;
        std::uint64_t eligibleGroupedFarObjects = 0;
        std::uint64_t culledGroupedFarObjects = 0;
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
        bool engineInitFlag = false;
    };

    StartupProfile g_profile;
    bool g_cullingEnabled = false;
    bool g_cullingRequested = false;
    bool g_lastEngineInitFlag = false;
    bool g_gpuObjectProbeStarted = false;

    // First experimental stage: skip only the draw traversal of distant,
    // off-screen GameObjects. Effects and triggers are always protected;
    // grouped objects are protected near the player and eligible when far away.
    bool shouldCullObject(GameObject* object) {
        if (!object || object->m_classType == GameObjectClassType::Effect || object->isTrigger()) {
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

    // Grouped objects can be moved or rotated by triggers. Keep them visible
    // around the player, but allow distant grouped objects to use the same
    // conservative off-screen test as ordinary objects. If player coordinates
    // cannot be resolved, fail safe and protect the grouped object.
    bool isGroupedObjectNearPlayer(GameObject* object) {
        auto* layer = g_profile.layer;
        if (!object || !layer || !layer->m_player1) return true;

        auto* objectParent = object->getParent();
        auto* playerParent = layer->m_player1->getParent();
        if (!objectParent || !playerParent) return true;

        auto const bounds = object->getBoundingRect();
        auto const localCenter = cocos2d::CCPoint(
            bounds.origin.x + bounds.size.width * 0.5f,
            bounds.origin.y + bounds.size.height * 0.5f
        );
        auto const objectWorld = objectParent->convertToWorldSpace(localCenter);
        auto const playerWorld = playerParent->convertToWorldSpace(layer->m_player1->getPosition());
        auto const screen = cocos2d::CCDirector::sharedDirector()->getWinSize();
        auto const radius = std::max(screen.width, screen.height) * 1.25f;
        auto const dx = objectWorld.x - playerWorld.x;
        auto const dy = objectWorld.y - playerWorld.y;
        return dx * dx + dy * dy <= radius * radius;
    }

    struct BoundsCandidateStats {
        std::uint64_t objects = 0;
        std::uint64_t valid = 0;
        std::uint64_t invalid = 0;
        std::uint64_t visible = 0;
        std::uint64_t offscreen = 0;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
    };

    struct BoundsValidation {
        BoundsCandidateStats parentTransformed;
        BoundsCandidateStats rawRect;
        BoundsCandidateStats nodeLocal;
        std::uint64_t scannedNodes = 0;
        std::uint64_t scannedGameObjects = 0;
    };

    bool intersectsExpandedViewport(
        float minX, float minY, float maxX, float maxY,
        float width, float height
    ) {
        if (!std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY) ||
            minX > maxX || minY > maxY) return false;
        auto const marginX = width * 0.75f;
        auto const marginY = height * 0.75f;
        return maxX >= -marginX && minX <= width + marginX &&
               maxY >= -marginY && minY <= height + marginY;
    }

    void recordBoundsCandidate(
        BoundsCandidateStats& stats,
        float minX, float minY, float maxX, float maxY,
        float width, float height
    ) {
        ++stats.objects;
        if (!std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY) ||
            minX > maxX || minY > maxY) {
            ++stats.invalid;
            return;
        }
        ++stats.valid;
        stats.minX = std::min(stats.minX, minX);
        stats.minY = std::min(stats.minY, minY);
        stats.maxX = std::max(stats.maxX, maxX);
        stats.maxY = std::max(stats.maxY, maxY);
        if (intersectsExpandedViewport(minX, minY, maxX, maxY, width, height)) {
            ++stats.visible;
        } else {
            ++stats.offscreen;
        }
    }

    BoundsValidation validateObjectBounds(CCNode* root, float width, float height) {
        BoundsValidation result;
        if (!root || width <= 0.f || height <= 0.f) return result;

        std::vector<CCNode*> pending{root};
        while (!pending.empty()) {
            auto* node = pending.back();
            pending.pop_back();
            if (!node) continue;
            ++result.scannedNodes;

            if (auto* object = typeinfo_cast<GameObject*>(node)) {
                ++result.scannedGameObjects;
                auto const rect = object->getBoundingRect();
                auto* parent = object->getParent();

                // Candidate A: interpret getBoundingRect() in parent space,
                // then transform all four corners through the parent.
                if (parent) {
                    auto const x1 = rect.origin.x;
                    auto const y1 = rect.origin.y;
                    auto const x2 = x1 + rect.size.width;
                    auto const y2 = y1 + rect.size.height;
                    auto const p1 = parent->convertToWorldSpace({x1, y1});
                    auto const p2 = parent->convertToWorldSpace({x2, y1});
                    auto const p3 = parent->convertToWorldSpace({x1, y2});
                    auto const p4 = parent->convertToWorldSpace({x2, y2});
                    recordBoundsCandidate(
                        result.parentTransformed,
                        std::min({p1.x,p2.x,p3.x,p4.x}),
                        std::min({p1.y,p2.y,p3.y,p4.y}),
                        std::max({p1.x,p2.x,p3.x,p4.x}),
                        std::max({p1.y,p2.y,p3.y,p4.y}), width, height
                    );
                } else {
                    ++result.parentTransformed.objects;
                    ++result.parentTransformed.invalid;
                }

                // Candidate B: treat the returned rectangle as already in
                // world/screen space. This helps detect double transforms.
                recordBoundsCandidate(
                    result.rawRect,
                    rect.getMinX(), rect.getMinY(),
                    rect.getMaxX(), rect.getMaxY(), width, height
                );

                // Candidate C: transform the node's own local content rect.
                auto const size = object->getContentSize();
                auto const anchor = object->getAnchorPoint();
                auto const lx1 = -anchor.x * size.width;
                auto const ly1 = -anchor.y * size.height;
                auto const lx2 = lx1 + size.width;
                auto const ly2 = ly1 + size.height;
                auto const q1 = object->convertToWorldSpace({lx1, ly1});
                auto const q2 = object->convertToWorldSpace({lx2, ly1});
                auto const q3 = object->convertToWorldSpace({lx1, ly2});
                auto const q4 = object->convertToWorldSpace({lx2, ly2});
                recordBoundsCandidate(
                    result.nodeLocal,
                    std::min({q1.x,q2.x,q3.x,q4.x}),
                    std::min({q1.y,q2.y,q3.y,q4.y}),
                    std::max({q1.x,q2.x,q3.x,q4.x}),
                    std::max({q1.y,q2.y,q3.y,q4.y}), width, height
                );
            }

            auto const count = node->getChildrenCount();
            if (count == 0 || !node->getChildren()) continue;
            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (child) pending.push_back(child);
            }
        }
        return result;
    }

    void reportBoundsCandidate(char const* name, BoundsCandidateStats const& s) {
        log::info(
            "DashCore 0.7.4 BOUNDS {}: objects={}, valid={}, invalid={}, visibleWithMargin={}, offscreenWithMargin={}, extents=({:.1f},{:.1f})-({:.1f},{:.1f}).",
            name, s.objects, s.valid, s.invalid, s.visible, s.offscreen,
            s.valid ? s.minX : 0.f, s.valid ? s.minY : 0.f,
            s.valid ? s.maxX : 0.f, s.valid ? s.maxY : 0.f
        );
    }


    struct ObjectGeometryStats {
        std::uint64_t objects = 0;
        std::uint64_t triggers = 0;
        std::uint64_t effects = 0;
        std::uint64_t grouped = 0;
        std::uint64_t ungrouped = 0;
        std::uint64_t parentless = 0;
        std::uint64_t selfHidden = 0;
        std::uint64_t ancestorHidden = 0;
        std::uint64_t invalidRect = 0;
        std::uint64_t zeroRect = 0;
        std::uint64_t negativeRect = 0;
        std::uint64_t invalidContent = 0;
        std::uint64_t zeroContent = 0;
        std::uint64_t scaledObjects = 0;
        std::uint64_t rotatedObjects = 0;
        std::uint64_t centerComparisons = 0;
        double centerDeltaTotal = 0.0;
        float centerDeltaMax = 0.f;
        float maxAbsRotation = 0.f;
        float minScaleMagnitude = std::numeric_limits<float>::max();
        float maxScaleMagnitude = 0.f;
        std::uint64_t scannedNodes = 0;
    };

    ObjectGeometryStats inspectObjectGeometry(CCNode* root) {
        ObjectGeometryStats stats;
        if (!root) return stats;

        std::vector<CCNode*> pending{root};
        while (!pending.empty()) {
            auto* node = pending.back();
            pending.pop_back();
            if (!node) continue;
            ++stats.scannedNodes;

            if (auto* object = typeinfo_cast<GameObject*>(node)) {
                ++stats.objects;
                if (object->isTrigger()) ++stats.triggers;
                if (object->m_classType == GameObjectClassType::Effect) ++stats.effects;
                if (object->m_groupCount > 0) ++stats.grouped;
                else ++stats.ungrouped;
                if (!object->isVisible()) ++stats.selfHidden;

                bool ancestorHidden = false;
                for (auto* ancestor = object->getParent(); ancestor; ancestor = ancestor->getParent()) {
                    if (!ancestor->isVisible()) {
                        ancestorHidden = true;
                        break;
                    }
                }
                if (ancestorHidden) ++stats.ancestorHidden;

                auto const rect = object->getBoundingRect();
                auto const rectFinite =
                    std::isfinite(rect.origin.x) && std::isfinite(rect.origin.y) &&
                    std::isfinite(rect.size.width) && std::isfinite(rect.size.height);
                if (!rectFinite) ++stats.invalidRect;
                else {
                    if (rect.size.width == 0.f || rect.size.height == 0.f) ++stats.zeroRect;
                    if (rect.size.width < 0.f || rect.size.height < 0.f) ++stats.negativeRect;
                }

                auto const size = object->getContentSize();
                auto const contentFinite =
                    std::isfinite(size.width) && std::isfinite(size.height);
                if (!contentFinite) ++stats.invalidContent;
                else if (size.width <= 0.f || size.height <= 0.f) ++stats.zeroContent;

                auto const sx = object->getScaleX();
                auto const sy = object->getScaleY();
                auto const scaleMagnitude = std::max(std::fabs(sx), std::fabs(sy));
                if (std::isfinite(scaleMagnitude)) {
                    stats.minScaleMagnitude = std::min(stats.minScaleMagnitude, scaleMagnitude);
                    stats.maxScaleMagnitude = std::max(stats.maxScaleMagnitude, scaleMagnitude);
                    if (std::fabs(sx - 1.f) > 0.001f || std::fabs(sy - 1.f) > 0.001f) {
                        ++stats.scaledObjects;
                    }
                } else {
                    ++stats.invalidContent;
                }

                auto const rotation = object->getRotation();
                if (std::isfinite(rotation)) {
                    auto const absRotation = std::fabs(rotation);
                    stats.maxAbsRotation = std::max(stats.maxAbsRotation, absRotation);
                    if (absRotation > 0.01f) ++stats.rotatedObjects;
                }

                // Compare the center implied by getBoundingRect() in parent
                // coordinates with the node's world-space anchor position.
                // This is diagnostic only; it does not select a culling bound.
                if (auto* parent = object->getParent(); parent && rectFinite) {
                    auto const rectCenter = cocos2d::CCPoint(
                        rect.origin.x + rect.size.width * 0.5f,
                        rect.origin.y + rect.size.height * 0.5f
                    );
                    auto const rectWorld = parent->convertToWorldSpace(rectCenter);
                    auto const anchorWorld = object->convertToWorldSpace({0.f, 0.f});
                    auto const dx = rectWorld.x - anchorWorld.x;
                    auto const dy = rectWorld.y - anchorWorld.y;
                    auto const delta = std::sqrt(dx * dx + dy * dy);
                    if (std::isfinite(delta)) {
                        ++stats.centerComparisons;
                        stats.centerDeltaTotal += delta;
                        stats.centerDeltaMax = std::max(stats.centerDeltaMax, delta);
                    }
                } else if (!object->getParent()) {
                    ++stats.parentless;
                }
            }

            auto const count = node->getChildrenCount();
            if (count == 0 || !node->getChildren()) continue;
            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (child) pending.push_back(child);
            }
        }
        return stats;
    }

    void reportObjectGeometry(ObjectGeometryStats const& s) {
        log::info(
            "DashCore 0.7.4 OBJECT-GEOMETRY: scannedNodes={}, objects={}, triggers={}, effects={}, grouped={}, ungrouped={}, parentless={}, selfHidden={}, ancestorHidden={}, invalidRect={}, zeroRect={}, negativeRect={}, invalidContent={}, zeroContent={}, scaled={}, rotated={}, maxAbsRotation={:.2f}, scaleMagnitudeRange={:.3f}-{:.3f}, centerComparisons={}, centerDeltaAvg={:.3f}, centerDeltaMax={:.3f}.",
            s.scannedNodes, s.objects, s.triggers, s.effects, s.grouped,
            s.ungrouped, s.parentless, s.selfHidden, s.ancestorHidden,
            s.invalidRect, s.zeroRect, s.negativeRect, s.invalidContent,
            s.zeroContent, s.scaledObjects, s.rotatedObjects,
            s.maxAbsRotation,
            s.minScaleMagnitude == std::numeric_limits<float>::max() ? 0.f : s.minScaleMagnitude,
            s.maxScaleMagnitude, s.centerComparisons,
            s.centerComparisons ? s.centerDeltaTotal / s.centerComparisons : 0.0,
            s.centerDeltaMax
        );
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

    // Build a one-shot GPU test batch from real GameObjects in the scene graph.
    // This is read-only: the returned bounds are uploaded for diagnostics only.
    std::vector<dashcore::bismuth::GpuCullInput> collectGpuCullInputs(CCNode* root) {
        std::vector<dashcore::bismuth::GpuCullInput> result;
        if (!root) return result;

        std::vector<std::pair<CCNode*, std::uint32_t>> pending;
        pending.emplace_back(root, 0);
        while (!pending.empty()) {
            auto const entry = pending.back();
            pending.pop_back();
            auto* node = entry.first;
            if (!node) continue;

            if (auto* object = typeinfo_cast<GameObject*>(node)) {
                dashcore::bismuth::GpuCullInput input;
                auto* parent = object->getParent();
                auto const bounds = object->getBoundingRect();

                bool const protectedObject =
                    object->m_classType == GameObjectClassType::Effect ||
                    object->isTrigger() ||
                    (object->m_groupCount > 0 && isGroupedObjectNearPlayer(object));

                if (!parent) {
                    input.flags = 1u;
                } else {
                    auto const x1 = bounds.origin.x;
                    auto const y1 = bounds.origin.y;
                    auto const x2 = x1 + bounds.size.width;
                    auto const y2 = y1 + bounds.size.height;
                    auto const p1 = parent->convertToWorldSpace({ x1, y1 });
                    auto const p2 = parent->convertToWorldSpace({ x2, y1 });
                    auto const p3 = parent->convertToWorldSpace({ x1, y2 });
                    auto const p4 = parent->convertToWorldSpace({ x2, y2 });
                    input.minX = std::min({ p1.x, p2.x, p3.x, p4.x });
                    input.maxX = std::max({ p1.x, p2.x, p3.x, p4.x });
                    input.minY = std::min({ p1.y, p2.y, p3.y, p4.y });
                    input.maxY = std::max({ p1.y, p2.y, p3.y, p4.y });
                }

                if (protectedObject) input.flags |= 1u;
                result.push_back(input);
            }

            auto const childCount = node->getChildrenCount();
            if (childCount == 0 || !node->getChildren()) continue;
            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (child) pending.emplace_back(child, entry.second + 1);
            }
        }
        return result;
    }

    void reportSceneGraph(char const* stage) {
        log::info(
            "DashCore 0.7.4 SCENE-GRAPH {}: scans={}, nodes={}, gameObjects={}, effects={}, grouped={}, hiddenGameObjects={}, maxChildren={}, maxDepth={}, lastScan={}us, engineInitFlag={}.",
            stage, g_profile.sceneScans, g_profile.sceneNodes,
            g_profile.sceneGameObjects, g_profile.sceneEffectObjects,
            g_profile.sceneGroupedObjects, g_profile.sceneHiddenGameObjects,
            g_profile.sceneMaxChildren, g_profile.sceneMaxDepth,
            g_profile.sceneScanUs, g_profile.engineInitFlag
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
            "DashCore 0.7.4 {} buckets: <=2ms={}, 2-4ms={}, 4-8ms={}, 8-16ms={}, 16-25ms={}, >25ms={}.",
            label, b[0], b[1], b[2], b[3], b[4], b[5]
        );
    }

    void logGapBuckets(char const* label, Buckets const& b) {
        log::info(
            "DashCore 0.7.4 {} buckets: <=16.67ms={}, 16.67-25ms={}, 25-50ms={}, 50-100ms={}, >100ms={}.",
            label, b[0], b[1], b[2], b[3], b[4]
        );
    }

    void maybeFinishProfile() {
        if (g_profile.updateFrames >= kFramesToMeasure &&
            g_profile.drawFrames >= kFramesToMeasure) {
            log::info("DashCore 0.7.4: both 180-frame samples complete; diagnostic-only.");
            g_profile.layer = nullptr;
        }
    }

    void reportUpdateProfile(char const* stage) {
        if (g_profile.updateFrames == 0) return;
        log::info(
            "DashCore 0.7.4 UPDATE {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
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
            "DashCore 0.7.4 CULL {}: requested={}, active={}, frames={}, visited={}, eligible={}, protectedEffect={}, protectedTrigger={}, protectedGroupedNear={}, eligibleGroupedFar={}, culledGroupedFar={}, boundsChecks={}, culled={}.",
            stage, g_cullingRequested, g_cullingEnabled, g_profile.drawFrames,
            g_profile.visitedObjects, g_profile.eligibleObjects,
            g_profile.protectedEffectObjects, g_profile.protectedTriggerObjects,
            g_profile.protectedGroupedNearObjects, g_profile.eligibleGroupedFarObjects,
            g_profile.culledGroupedFarObjects, g_profile.boundsChecks, g_profile.culledObjects
        );
    }

    void reportDrawProfile(char const* stage) {
        if (g_profile.drawFrames == 0) return;
        log::info(
            "DashCore 0.7.4 DRAWSCENE-CPU {}: frames={}, avg={}us, max={}us, over8ms={}, gaps>25ms={}, maxGap={}us. GPU execution time is not measured.",
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
        bool const profiling = g_profile.layer != nullptr &&
            g_profile.drawFrames < kFramesToMeasure;
        bool const sampleVisit = profiling &&
            (g_profile.drawFrames % kReportEvery == 0);

        // With live culling disabled, perform RTTI and classification only on
        // diagnostic sample frames. If culling is ever enabled in a future
        // validated build, classification still runs each frame as required.
        if (!g_cullingEnabled && !sampleVisit) {
            CCNode::visit();
            return;
        }

        auto* object = typeinfo_cast<GameObject*>(this);
        if (!object) {
            CCNode::visit();
            return;
        }

        if (sampleVisit) ++g_profile.visitedObjects;

        bool const isEffect = object->m_classType == GameObjectClassType::Effect;
        bool const isTrigger = object->isTrigger();
        bool const isGrouped = object->m_groupCount > 0;
        bool const groupedNearPlayer = isGrouped && isGroupedObjectNearPlayer(object);
        bool const eligible = !isEffect && !isTrigger && !groupedNearPlayer;

        // Every diagnostic category is counted on exactly the same sampled
        // visits, so the totals are comparable instead of mixing one-frame
        // samples with 180-frame accumulated counters.
        if (sampleVisit) {
            if (isEffect) {
                ++g_profile.protectedEffectObjects;
            } else if (isTrigger) {
                ++g_profile.protectedTriggerObjects;
            } else if (groupedNearPlayer) {
                ++g_profile.protectedGroupedNearObjects;
            } else {
                ++g_profile.eligibleObjects;
                if (isGrouped) ++g_profile.eligibleGroupedFarObjects;
                if (g_cullingEnabled) ++g_profile.boundsChecks;
            }
        }

        if (!g_cullingEnabled) {
            CCNode::visit();
            return;
        }

        // This branch is deliberately unreachable in 0.7.4: live culling is
        // hard-disabled until the object collector and bounds are validated.
        if (eligible && shouldCullObject(object)) {
            if (sampleVisit) {
                ++g_profile.culledObjects;
                if (isGrouped) ++g_profile.culledGroupedFarObjects;
            }
            return;
        }

        CCNode::visit();
    }
};

class $modify(OptGodPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_profile = {};
        g_gpuObjectProbeStarted = false;
        g_lastEngineInitFlag = dontCreateObjects;

        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        if (result) {
            log::info(
                "DashCore 0.7.4: PlayLayer::init={}us ({}ms), levelID={}, replay={}, engineInitFlag={}. ",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("DashCore 0.7.4: PlayLayer::init failed after {}us.", elapsedUs);
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
        // It remains opt-in while grouped-object behavior is validated.
        g_cullingEnabled = false; // 0.7.4 safety lock: never alter live rendering.
        g_profile.engineInitFlag = g_lastEngineInitFlag;
        log::info(
            "DashCore 0.7.4: setupHasCompleted={}us ({}ms); culling-requested={}, culling-active={}, engineInitFlag={} (raw engine argument; not a gameplay-mode detector). Live render culling is hard-disabled in 0.7.4 while scene coverage and bounds are validated.",
            elapsedUs, elapsedUs / 1000, g_cullingRequested, g_cullingEnabled,
            g_profile.engineInitFlag
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
        // Validate the Android GLES compute/SSBO path once, with GL context active.
        // The probe restores the GL state it touches and never replaces GD draws.
        dashcore::bismuth::runGLESComputeProbeOnce();
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
            // Refresh the scene snapshot at the exact end of the 180-frame
            // window. Geometry Dash may continue populating object nodes
            // during early gameplay, so older snapshots are not comparable.
            auto const finalScan = inspectSceneGraph(g_profile.layer);
            g_profile.sceneNodes = finalScan.nodes;
            g_profile.sceneGameObjects = finalScan.gameObjects;
            g_profile.sceneEffectObjects = finalScan.effectObjects;
            g_profile.sceneGroupedObjects = finalScan.groupedObjects;
            g_profile.sceneHiddenGameObjects = finalScan.hiddenGameObjects;
            g_profile.sceneMaxChildren = finalScan.maxChildren;
            g_profile.sceneMaxDepth = finalScan.maxDepth;
            reportDrawProfile("FINAL");
            reportSceneGraph("FINAL-REFRESHED");
        }
        // Wait until the full 180-frame sample has completed before collecting
        // GPU inputs. The previous build collected on frame 1, before the
        // level's object graph had finished populating (only 2 objects seen).
        // Use the exact same PlayLayer root as inspectSceneGraph and compare
        // both traversals before running the read-only GLES probe.
        if (g_profile.layer && g_profile.drawFrames == kFramesToMeasure &&
            !g_gpuObjectProbeStarted) {
            g_gpuObjectProbeStarted = true;
            auto* root = static_cast<CCNode*>(g_profile.layer);
            auto const scanStart = Clock::now();
            auto const scan = inspectSceneGraph(root);
            auto const scanUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - scanStart).count();

            auto const collectStart = Clock::now();
            auto const inputs = collectGpuCullInputs(root);
            auto const collectUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - collectStart).count();

            auto const screen = cocos2d::CCDirector::sharedDirector()->getWinSize();
            auto const boundsStart = Clock::now();
            auto const bounds = validateObjectBounds(root, screen.width, screen.height);
            auto const boundsUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - boundsStart).count();

            auto const geometryStart = Clock::now();
            auto const geometry = inspectObjectGeometry(root);
            auto const geometryUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - geometryStart).count();

            log::info(
                "DashCore 0.7.4 DIAGNOSTIC-COST: sceneScan={}us, gpuInputCollection={}us, boundsValidation={}us, objectGeometry={}us, gpuInputs={}, boundsObjects={}.",
                scanUs, collectUs, boundsUs, geometryUs, inputs.size(), bounds.scannedGameObjects
            );
            log::info(
                "DashCore 0.7.4 BISMUTH-CULL: same-frame traversal check; rootChildren={}, scannedNodes={}, scannedGameObjects={}, collectedGameObjects={}, boundsScannedNodes={}, boundsScannedObjects={}, geometryScannedNodes={}, geometryObjects={}, traversalDelta={}.",
                root->getChildrenCount(), scan.nodes, scan.gameObjects,
                inputs.size(), bounds.scannedNodes, bounds.scannedGameObjects,
                geometry.scannedNodes, geometry.objects,
                static_cast<std::int64_t>(scan.gameObjects) -
                    static_cast<std::int64_t>(inputs.size())
            );
            reportBoundsCandidate("parent-transformed", bounds.parentTransformed);
            reportBoundsCandidate("raw-rect", bounds.rawRect);
            reportBoundsCandidate("node-local", bounds.nodeLocal);
            reportObjectGeometry(geometry);
            dashcore::bismuth::runGLESObjectCullingProbeOnce(
                inputs, screen.width, screen.height
            );
        }
        maybeFinishProfile();
    }
};

$on_mod(Loaded) {
    log::info(
        "DashCore 0.7.4 build fix: visibility-pass hook removed because Geode bindings define updateVisibility inline; frame and draw profiling remain enabled."
    );
}
