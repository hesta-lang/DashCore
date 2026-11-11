#include <Geode/Geode.hpp>
#include "bismuth/GLESComputeProbe.hpp"
#include "bismuth/GLESObjectCullingProbe.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>
#include <fstream>
#include <string>

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
        std::uint32_t updateGapsOver50ms = 0;
        std::uint32_t updateGapsOver100ms = 0;
        std::uint32_t drawGapsOver50ms = 0;
        std::uint32_t drawGapsOver100ms = 0;
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
        std::vector<std::int64_t> updateGapSamples;
        std::vector<std::int64_t> drawGapSamples;
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
        std::uint64_t sampledVisitCalls = 0;
        bool nodeVisitReported = false;
        std::int64_t sampledVisitTotalUs = 0;
        std::int64_t sampledVisitMaxUs = 0;
        std::uint64_t sampledGameObjectVisits = 0;
        std::int64_t sampledGameObjectVisitTotalUs = 0;
        std::int64_t sampledGameObjectVisitMaxUs = 0;
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
    bool g_fastPathEnabled = true; // Default production behavior; AutoTest overrides this at startup.
    bool g_generalEnabled = true; // Master switch: keep the mod loaded but bypass all DashCore behavior.
    bool g_diagnosticsEnabled = false; // Expensive profiling and scene/GPU probes are opt-in.
    bool g_cullingEnabled = false;
    bool g_cullingRequested = false;
    bool g_lastEngineInitFlag = false;
    bool g_gpuObjectProbeStarted = false;

    // High-frequency hooks are opt-in: when diagnostics are OFF, these hooks
    // are not installed, removing their detour overhead from normal gameplay.
    Hook* g_nodeVisitHook = nullptr;
    Hook* g_postUpdateHook = nullptr;
    Hook* g_drawSceneHook = nullptr;

    void enableDiagnosticHook(Hook* hook, char const* name) {
        if (!hook) {
            log::error("DashCore 0.9.4-alpha: diagnostic hook '{}' was not registered.", name);
            return;
        }
        auto result = hook->enable();
        if (!result) {
            log::error("DashCore 0.9.4-alpha: could not enable diagnostic hook '{}'.", name);
            return;
        }
        log::info("DashCore 0.9.4-alpha: diagnostic hook '{}' enabled.", name);
    }

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
            "DashCore 0.9.4-alpha BOUNDS {}: objects={}, valid={}, invalid={}, visibleWithMargin={}, offscreenWithMargin={}, extents=({:.1f},{:.1f})-({:.1f},{:.1f}).",
            name, s.objects, s.valid, s.invalid, s.visible, s.offscreen,
            s.valid ? s.minX : 0.f, s.valid ? s.minY : 0.f,
            s.valid ? s.maxX : 0.f, s.valid ? s.maxY : 0.f
        );
    }


    struct ObjectGeometryStats {
        std::uint64_t objects = 0;
        std::uint64_t triggers = 0;
        std::uint64_t triggerSamples = 0;
        std::map<int, std::uint64_t> classTypeCounts;
        std::map<int, std::uint64_t> triggerClassTypeCounts;
        std::map<int, std::uint64_t> objectIDCounts;
        std::map<int, std::uint64_t> triggerObjectIDCounts;
        std::uint64_t groupedSamples = 0;
        std::uint32_t minGroupCount = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t maxGroupCount = 0;
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
        std::uint64_t rawCandidateOffscreenTriggers = 0;
        std::uint64_t rawCandidateOffscreenEffects = 0;
        std::uint64_t rawCandidateOffscreenGrouped = 0;
        std::uint64_t rawCandidateOffscreenOrdinary = 0;
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

        std::uint32_t detailedSamples = 0;
        std::uint32_t objectIdSamples = 0;
        auto const screen = cocos2d::CCDirector::sharedDirector()->getWinSize();
        std::vector<CCNode*> pending{root};
        while (!pending.empty()) {
            auto* node = pending.back();
            pending.pop_back();
            if (!node) continue;
            ++stats.scannedNodes;

            if (auto* object = typeinfo_cast<GameObject*>(node)) {
                ++stats.objects;
                auto const classType = static_cast<int>(object->m_classType);
                ++stats.classTypeCounts[classType];
                auto const objectID = static_cast<int>(object->m_objectID);
                ++stats.objectIDCounts[objectID];
                if (object->isTrigger()) {
                    ++stats.triggerObjectIDCounts[objectID];
                    ++stats.triggers;
                    ++stats.triggerClassTypeCounts[classType];
                }
                if (object->m_classType == GameObjectClassType::Effect) ++stats.effects;
                auto const groupCount = object->m_groupCount > 0 ? static_cast<std::uint32_t>(object->m_groupCount) : 0u;
                stats.minGroupCount = std::min(stats.minGroupCount, groupCount);
                stats.maxGroupCount = std::max(stats.maxGroupCount, groupCount);
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
                auto const sampleParent = object->getParent();
                auto const samplePos = object->getPosition();
                auto const sampleWorld = sampleParent ? sampleParent->convertToWorldSpace(samplePos) : samplePos;
                if (object->isTrigger() && stats.triggerSamples < 16) {
                    log::info("DashCore 0.9.4-alpha TRIGGER-SAMPLE {}: classType={}, groups={}, parent={}, pos=({:.2f},{:.2f}), world=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), scale=({:.3f},{:.3f}), rotation={:.2f}.",
                        stats.triggerSamples, static_cast<int>(object->m_classType), object->m_groupCount,
                        sampleParent != nullptr, samplePos.x, samplePos.y, sampleWorld.x, sampleWorld.y,
                        rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
                        object->getScaleX(), object->getScaleY(), object->getRotation());
                    ++stats.triggerSamples;
                }
                if (object->m_groupCount > 0 && stats.groupedSamples < 8) {
                    log::info("DashCore 0.9.4-alpha GROUPED-SAMPLE {}: trigger={}, classType={}, groups={}, parent={}, pos=({:.2f},{:.2f}), world=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), scale=({:.3f},{:.3f}), rotation={:.2f}.",
                        stats.groupedSamples, object->isTrigger(), static_cast<int>(object->m_classType),
                        object->m_groupCount, sampleParent != nullptr, samplePos.x, samplePos.y,
                        sampleWorld.x, sampleWorld.y, rect.origin.x, rect.origin.y, rect.size.width,
                        rect.size.height, object->getScaleX(), object->getScaleY(), object->getRotation());
                    ++stats.groupedSamples;
                }
                // Sample IDs only after the native trigger predicate identifies a trigger;
                // this avoids spending the bounded sample on unrelated early objects.
                if (object->isTrigger() && objectIdSamples < 24) {
                    log::info("DashCore 0.9.4-alpha TRIGGER-ID-SAMPLE {}: objectID={}, isTrigger={}, classType={}, groups={}, effect={}.",
                        objectIdSamples, objectID, object->isTrigger(), classType,
                        object->m_groupCount, object->m_classType == GameObjectClassType::Effect);
                    ++objectIdSamples;
                }
                // A bounded generic sample makes coordinate-space hypotheses testable.
                if (detailedSamples < 12) {
                    auto const pos = object->getPosition();
                    auto const parent = object->getParent();
                    auto const anchorWorld = parent ? parent->convertToWorldSpace(pos) : pos;
                    log::info(
                        "DashCore 0.9.4-alpha OBJECT-SAMPLE {}: trigger={}, classType={}, groups={}, parent={}, pos=({:.2f},{:.2f}), worldAnchor=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), objectScale=({:.3f},{:.3f}), objectRotation={:.2f}, parentScale=({:.3f},{:.3f}), parentRotation={:.2f}.",
                        detailedSamples, object->isTrigger(),
                        static_cast<int>(object->m_classType), object->m_groupCount,
                        parent != nullptr, pos.x, pos.y, anchorWorld.x, anchorWorld.y,
                        rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
                        object->getScaleX(), object->getScaleY(), object->getRotation(),
                        parent ? parent->getScaleX() : 1.f,
                        parent ? parent->getScaleY() : 1.f,
                        parent ? parent->getRotation() : 0.f
                    );
                    ++detailedSamples;
                }
                auto const rectFinite =
                    std::isfinite(rect.origin.x) && std::isfinite(rect.origin.y) &&
                    std::isfinite(rect.size.width) && std::isfinite(rect.size.height);
                if (!rectFinite) ++stats.invalidRect;
                else {
                    if (rect.size.width == 0.f || rect.size.height == 0.f) ++stats.zeroRect;
                    if (rect.size.width < 0.f || rect.size.height < 0.f) ++stats.negativeRect;

                    // Breakdown of the raw-rectangle candidate only. This
                    // deliberately does not declare that candidate correct.
                    bool const rawCandidateOffscreen = !intersectsExpandedViewport(
                        rect.getMinX(), rect.getMinY(), rect.getMaxX(), rect.getMaxY(),
                        screen.width, screen.height
                    );
                    if (rawCandidateOffscreen) {
                        if (object->isTrigger()) ++stats.rawCandidateOffscreenTriggers;
                        else if (object->m_classType == GameObjectClassType::Effect) ++stats.rawCandidateOffscreenEffects;
                        else if (object->m_groupCount > 0) ++stats.rawCandidateOffscreenGrouped;
                        else ++stats.rawCandidateOffscreenOrdinary;
                    }
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
                    auto const anchorWorld = parent->convertToWorldSpace(object->getPosition());
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
            "DashCore 0.9.4-alpha OBJECT-GEOMETRY: scannedNodes={}, objects={}, triggers={}, triggerSamples={}, effects={}, grouped={}, groupedSamples={}, ungrouped={}, groupCountRange={}-{}, parentless={}, selfHidden={}, ancestorHidden={}, invalidRect={}, zeroRect={}, negativeRect={}, invalidContent={}, zeroContent={}, scaled={}, rotated={}, maxAbsRotation={:.2f}, scaleMagnitudeRange={:.3f}-{:.3f}, rawRectCandidateOffscreen(trigger/effect/grouped/ordinary)={}/{}/{}/{}, centerComparisons={}, centerDeltaAvg={:.3f}, centerDeltaMax={:.3f}.",
            s.scannedNodes, s.objects, s.triggers, s.triggerSamples, s.effects, s.grouped, s.groupedSamples,
            s.ungrouped, s.objects ? s.minGroupCount : 0u, s.maxGroupCount, s.parentless, s.selfHidden, s.ancestorHidden,
            s.invalidRect, s.zeroRect, s.negativeRect, s.invalidContent,
            s.zeroContent, s.scaledObjects, s.rotatedObjects,
            s.maxAbsRotation,
            s.minScaleMagnitude == std::numeric_limits<float>::max() ? 0.f : s.minScaleMagnitude,
            s.maxScaleMagnitude,
            s.rawCandidateOffscreenTriggers, s.rawCandidateOffscreenEffects,
            s.rawCandidateOffscreenGrouped, s.rawCandidateOffscreenOrdinary,
            s.centerComparisons,
            s.centerComparisons ? s.centerDeltaTotal / s.centerComparisons : 0.0,
            s.centerDeltaMax
        );
        for (auto const& [objectID, count] : s.objectIDCounts) {
            auto const triggerIt = s.triggerObjectIDCounts.find(objectID);
            auto const triggerCount = triggerIt == s.triggerObjectIDCounts.end() ? 0u : triggerIt->second;
            log::info("DashCore 0.9.4-alpha OBJECT-ID: objectID={}, objects={}, isTriggerTrue={}",
                objectID, count, triggerCount);
        }
        for (auto const& [type, count] : s.classTypeCounts) {
            auto const triggerIt = s.triggerClassTypeCounts.find(type);
            auto const triggerCount = triggerIt == s.triggerClassTypeCounts.end() ? 0u : triggerIt->second;
            log::info("DashCore 0.9.4-alpha OBJECT-TYPE: classType={}, objects={}, isTriggerTrue={}.",
                type, count, triggerCount);
        }
        if (s.triggers == 0) {
            log::info("DashCore 0.9.4-alpha TRIGGER-DIAGNOSTIC: isTrigger() returned false for every GameObject in this snapshot; this does not prove the level has no trigger objects.");
        }
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

        std::uint64_t triggerCount = 0, effectCount = 0, groupedCount = 0;
        std::uint64_t groupedNearCount = 0, groupedFarCount = 0;
        std::uint64_t parentlessCount = 0, invalidBoundsCount = 0;
        std::vector<std::pair<CCNode*, std::uint32_t>> pending;
        pending.emplace_back(root, 0);
        while (!pending.empty()) {
            auto const entry = pending.back();
            pending.pop_back();
            auto* node = entry.first;
            if (!node) continue;

            if (auto* object = typeinfo_cast<GameObject*>(node)) {
                dashcore::bismuth::GpuCullInput input;
                bool const isTrigger = object->isTrigger();
                bool const isEffect = object->m_classType == GameObjectClassType::Effect;
                bool const isGrouped = object->m_groupCount > 0;
                bool const groupedNear = isGrouped && isGroupedObjectNearPlayer(object);
                // Group transforms are trigger-driven and not yet mirrored by
                // this adapter. Protect every grouped object, not only nearby ones.
                bool const protectedObject = isEffect || isTrigger || isGrouped;
                if (isTrigger) { ++triggerCount; input.flags |= (1u << 1); }
                if (isEffect) { ++effectCount; input.flags |= (1u << 2); }
                if (isGrouped) {
                    ++groupedCount;
                    input.flags |= (1u << 3);
                    if (groupedNear) { ++groupedNearCount; input.flags |= (1u << 4); }
                    else ++groupedFarCount;
                }

                auto const size = object->getContentSize();
                auto const anchor = object->getAnchorPoint();
                auto* parent = object->getParent();
                if (!parent) {
                    ++parentlessCount;
                    input.flags |= (1u << 5);
                    input.flags |= 1u;
                } else if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
                           size.width <= 0.f || size.height <= 0.f) {
                    ++invalidBoundsCount;
                    input.flags |= (1u << 6);
                    input.flags |= 1u;
                } else {
                    // Use the node's own local content rectangle and let Cocos
                    // apply the complete object + ancestor transform chain.
                    auto const x1 = -anchor.x * size.width;
                    auto const y1 = -anchor.y * size.height;
                    auto const x2 = x1 + size.width;
                    auto const y2 = y1 + size.height;
                    auto const p1 = object->convertToWorldSpace({ x1, y1 });
                    auto const p2 = object->convertToWorldSpace({ x2, y1 });
                    auto const p3 = object->convertToWorldSpace({ x1, y2 });
                    auto const p4 = object->convertToWorldSpace({ x2, y2 });
                    input.minX = std::min({ p1.x, p2.x, p3.x, p4.x });
                    input.maxX = std::max({ p1.x, p2.x, p3.x, p4.x });
                    input.minY = std::min({ p1.y, p2.y, p3.y, p4.y });
                    input.maxY = std::max({ p1.y, p2.y, p3.y, p4.y });
                    bool const validBounds = std::isfinite(input.minX) && std::isfinite(input.minY) &&
                        std::isfinite(input.maxX) && std::isfinite(input.maxY) &&
                        input.minX <= input.maxX && input.minY <= input.maxY;
                    if (!validBounds) {
                        ++invalidBoundsCount;
                        input.flags |= (1u << 6);
                        input.flags |= 1u;
                    }
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
        log::info("DashCore 0.9.4-alpha GPU-INPUT-CLASSIFICATION: source=node-local-content-world, all-grouped-forced-visible: objects={}, triggers={}, effects={}, grouped={}, groupedNearProtected={}, groupedFarCandidate={}, parentlessForcedVisible={}, invalidBoundsForcedVisible={}. Flags: forceVisible=bit0, trigger=bit1, effect=bit2, grouped=bit3, groupedNear=bit4, parentless=bit5, invalidBounds=bit6.",
            result.size(), triggerCount, effectCount, groupedCount, groupedNearCount, groupedFarCount,
            parentlessCount, invalidBoundsCount);
        return result;
    }

    void reportSceneGraph(char const* stage) {
        log::info(
            "DashCore 0.9.4-alpha SCENE-GRAPH {}: scans={}, nodes={}, gameObjects={}, effects={}, grouped={}, hiddenGameObjects={}, maxChildren={}, maxDepth={}, lastScan={}us, engineInitFlag={}.",
            stage, g_profile.sceneScans, g_profile.sceneNodes,
            g_profile.sceneGameObjects, g_profile.sceneEffectObjects,
            g_profile.sceneGroupedObjects, g_profile.sceneHiddenGameObjects,
            g_profile.sceneMaxChildren, g_profile.sceneMaxDepth,
            g_profile.sceneScanUs, g_profile.engineInitFlag
        );
    }

    constexpr std::uint32_t kFramesToMeasure = 600;
    constexpr std::uint32_t kReportEvery = 120;
    constexpr std::int64_t kSlowCallbackUs = 8000;
    constexpr std::int64_t kLongFrameGapUs = 50000;
    constexpr std::int64_t kSevereFrameGapUs = 100000;

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
            "DashCore 0.9.4-alpha {} buckets: <=2ms={}, 2-4ms={}, 4-8ms={}, 8-16ms={}, 16-25ms={}, >25ms={}.",
            label, b[0], b[1], b[2], b[3], b[4], b[5]
        );
    }

    std::int64_t gapPercentile(std::vector<std::int64_t> samples, std::size_t percentile) {
        if (samples.empty()) return 0;
        std::sort(samples.begin(), samples.end());
        auto const index = std::min(samples.size() - 1, (samples.size() - 1) * percentile / 100);
        return samples[index];
    }

    void logGapBuckets(char const* label, Buckets const& b,
                       std::vector<std::int64_t> const& samples,
                       std::uint32_t over50ms, std::uint32_t over100ms) {
        log::info(
            "DashCore 0.9.4-alpha {} callback intervals: <=16.67ms={}, 16.67-25ms={}, 25-50ms={}, 50-100ms={}, >100ms={}, >50ms={}, >100ms={}, p50={}us, p95={}us, p99={}us.",
            label, b[0], b[1], b[2], b[3], b[4], over50ms, over100ms,
            gapPercentile(samples, 50), gapPercentile(samples, 95), gapPercentile(samples, 99)
        );
    }

    void maybeFinishProfile() {
        if (g_profile.updateFrames >= kFramesToMeasure &&
            g_profile.drawFrames >= kFramesToMeasure) {
            log::info("DashCore 0.9.4-alpha: both 600-frame samples complete; diagnostic-only.");
            g_profile.layer = nullptr;
        }
    }

    void reportUpdateProfile(char const* stage) {
        if (g_profile.updateFrames == 0) return;
        log::info(
            "DashCore 0.9.4-alpha UPDATE {}: frames={}, avg={}us, max={}us, over8ms={}, callbackGap>50ms={}, callbackGap>100ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
            stage, g_profile.updateFrames,
            g_profile.totalPostUpdateUs / g_profile.updateFrames,
            g_profile.maxPostUpdateUs, g_profile.slowPostUpdates,
            g_profile.updateGapsOver50ms, g_profile.updateGapsOver100ms, g_profile.maxUpdateGapUs,
            g_profile.minDt, g_profile.maxDt
        );
        logBuckets("UPDATE callback-cost", g_profile.updateDurationBuckets);
        logGapBuckets("UPDATE", g_profile.updateGapBuckets, g_profile.updateGapSamples,
            g_profile.updateGapsOver50ms, g_profile.updateGapsOver100ms);
    }

    void reportVisitCost(char const* stage) {
        log::info("DashCore 0.9.4-alpha NODE-VISIT-COST {}: sampledNodeVisits={}, inclusiveTotal={}us, inclusiveMax={}us, sampledGameObjectVisits={}, gameObjectInclusiveTotal={}us, gameObjectInclusiveMax={}us. Inclusive totals overlap nested parent/child visits and must not be added to DrawScene CPU time.",
            stage, g_profile.sampledVisitCalls, g_profile.sampledVisitTotalUs,
            g_profile.sampledVisitMaxUs, g_profile.sampledGameObjectVisits,
            g_profile.sampledGameObjectVisitTotalUs, g_profile.sampledGameObjectVisitMaxUs);
    }

    void reportCullStats(char const* stage) {
        log::info(
            "DashCore 0.9.4-alpha CULL {}: requested={}, active={}, frames={}, visited={}, eligible={}, protectedEffect={}, protectedTrigger={}, protectedGroupedNear={}, eligibleGroupedFar={}, culledGroupedFar={}, boundsChecks={}, culled={}.",
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
            "DashCore 0.9.4-alpha DRAWSCENE-CPU {}: frames={}, avg={}us, max={}us, over8ms={}, callbackGap>50ms={}, callbackGap>100ms={}, maxGap={}us. GPU execution time is not measured.",
            stage, g_profile.drawFrames,
            g_profile.totalDrawSceneUs / g_profile.drawFrames,
            g_profile.maxDrawSceneUs, g_profile.slowDrawScenes,
            g_profile.drawGapsOver50ms, g_profile.drawGapsOver100ms, g_profile.maxDrawGapUs
        );
        logBuckets("DRAWSCENE callback-cost", g_profile.drawDurationBuckets);
        reportCullStats(stage);
        logGapBuckets("DRAWSCENE", g_profile.drawGapBuckets, g_profile.drawGapSamples,
            g_profile.drawGapsOver50ms, g_profile.drawGapsOver100ms);
    }
    bool g_lastLevelLaunchAttempted = false;

    void rememberLastPlayedLevel(GJGameLevel* level) {
        if (!level) return;

        // Daily/event and gauntlet entries can expire or require a separate
        // manager lookup. Keep this first version to persistent standard levels.
        if (level->m_dailyID.value() > 0 || level->m_gauntletLevel) return;

        auto const id = level->m_levelID.value();
        if (id <= 0) return;

        auto* mod = Mod::get();
        bool const isMainLevel = level->m_levelType == GJLevelType::Main;
        mod->setSavedValue<int>("last-level-id", id);
        mod->setSavedValue<bool>("last-level-main", isMainLevel);
        log::info("DashCore: remembered last level '{}' (id={}, main={}).",
            level->m_levelName, id, isMainLevel);
    }

}

// GameObject inherits the Cocos visit implementation instead of providing
// its own hookable visit method. Hook CCNode::visit and identify GameObjects
// with Geode's runtime type information before applying the culling test.
class $modify(OptGodCCNode, cocos2d::CCNode) {
    static void onModify(auto& self) {
        auto result = self.getHook("CCNode::visit");
        if (!result) {
            log::error("DashCore 0.9.4-alpha: could not register CCNode::visit diagnostic hook.");
            return;
        }
        g_nodeVisitHook = result.unwrap();
        g_nodeVisitHook->setAutoEnable(false);
    }

    void visit() {
        if (!g_generalEnabled || (!g_diagnosticsEnabled && !g_cullingEnabled)) {
            CCNode::visit();
            return;
        }
        // Once the diagnostic window is complete (or before gameplay starts),
        // avoid profile-state checks on every Cocos node visit. The original
        // renderer remains the only path; no visibility or draw behavior changes.
        if (g_fastPathEnabled && !g_cullingEnabled && g_profile.layer == nullptr) {
            CCNode::visit();
            return;
        }

        bool const profiling = g_profile.layer != nullptr &&
            g_profile.updateFrames < kFramesToMeasure;
        bool const sampleVisit = profiling &&
            (g_profile.updateFrames % kReportEvery == 0);
        if (g_profile.layer != nullptr &&
            g_profile.updateFrames >= kFramesToMeasure &&
            !g_profile.nodeVisitReported) {
            reportVisitCost("FINAL");
            g_profile.nodeVisitReported = true;
        }
        auto visitBase = [&]() {
            auto const visitStart = Clock::now();
            CCNode::visit();
            auto const visitUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - visitStart).count());
            ++g_profile.sampledVisitCalls;
            g_profile.sampledVisitTotalUs += visitUs;
            g_profile.sampledVisitMaxUs = std::max(g_profile.sampledVisitMaxUs, visitUs);
            if (typeinfo_cast<GameObject*>(this)) {
                ++g_profile.sampledGameObjectVisits;
                g_profile.sampledGameObjectVisitTotalUs += visitUs;
                g_profile.sampledGameObjectVisitMaxUs =
                    std::max(g_profile.sampledGameObjectVisitMaxUs, visitUs);
            }
        };

        // With live culling disabled, perform RTTI and classification only on
        // diagnostic sample frames. If culling is ever enabled in a future
        // validated build, classification still runs each frame as required.
        if (!g_cullingEnabled && !sampleVisit) {
            CCNode::visit();
            return;
        }

        auto* object = typeinfo_cast<GameObject*>(this);
        if (!object) {
            visitBase();
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
        // samples with 600-frame accumulated counters.
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
            visitBase();
            return;
        }

        // This branch is deliberately unreachable in 0.9.2-alpha: live culling is
        // hard-disabled until the object collector and bounds are validated.
        if (eligible && shouldCullObject(object)) {
            if (sampleVisit) {
                ++g_profile.culledObjects;
                if (isGrouped) ++g_profile.culledGroupedFarObjects;
            }
            return;
        }

        visitBase();
    }
};

class $modify(OptGodPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        auto result = self.getHook("PlayLayer::postUpdate");
        if (!result) {
            log::error("DashCore 0.9.4-alpha: could not register PlayLayer::postUpdate diagnostic hook.");
            return;
        }
        g_postUpdateHook = result.unwrap();
        g_postUpdateHook->setAutoEnable(false);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!g_generalEnabled) return PlayLayer::init(level, useReplay, dontCreateObjects);
        g_profile = {};
        g_gpuObjectProbeStarted = false;
        g_lastEngineInitFlag = dontCreateObjects;
        if (!g_diagnosticsEnabled) {
            bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
            if (result) rememberLastPlayedLevel(level);
            return result;
        }

        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start).count());

        if (result) {
            rememberLastPlayedLevel(level);
            log::info(
                "DashCore 0.9.4-alpha: PlayLayer::init={}us ({}ms), levelID={}, replay={}, engineInitFlag={}. ",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("DashCore 0.9.4-alpha: PlayLayer::init failed after {}us.", elapsedUs);
        }
        return result;
    }

    void setupHasCompleted() {
        if (!g_generalEnabled) {
            PlayLayer::setupHasCompleted();
            return;
        }
        if (!g_diagnosticsEnabled) {
            PlayLayer::setupHasCompleted();
            log::info("DashCore 0.9.4-alpha: setupHasCompleted (performance diagnostics OFF).");
            return;
        }
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
        g_cullingEnabled = false; // 0.9.2-alpha safety lock: never alter live rendering.
        g_profile.engineInitFlag = g_lastEngineInitFlag;
        log::info(
            "DashCore 0.9.4-alpha: setupHasCompleted={}us ({}ms); culling-requested={}, culling-active={}, engineInitFlag={} (raw engine argument; not a gameplay-mode detector). Live render culling is hard-disabled in 0.9.2-alpha while scene coverage and bounds are validated.",
            elapsedUs, elapsedUs / 1000, g_cullingRequested, g_cullingEnabled,
            g_profile.engineInitFlag
        );
    }

    void postUpdate(float dt) {
        if (!g_generalEnabled || !g_diagnosticsEnabled) {
            PlayLayer::postUpdate(dt);
            return;
        }
        bool const profiling = g_profile.layer == this &&
            g_profile.updateFrames < kFramesToMeasure;
        auto const start = Clock::now();

        if (profiling && g_profile.previousPostUpdate.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    start - g_profile.previousPostUpdate).count());
            g_profile.maxUpdateGapUs = std::max(g_profile.maxUpdateGapUs, gapUs);
            addGap(g_profile.updateGapBuckets, gapUs);
            g_profile.updateGapSamples.push_back(gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.updateGapsOver50ms;
            if (gapUs > kSevereFrameGapUs) ++g_profile.updateGapsOver100ms;
        }

        PlayLayer::postUpdate(dt);
        // Fallback if the scene stops issuing CCNode::visit callbacks.
        // Normally the first node visit after update 600 emits the final report.
        if (g_profile.layer == this &&
            g_profile.updateFrames >= kFramesToMeasure &&
            !g_profile.nodeVisitReported) {
            reportVisitCost("FINAL");
            g_profile.nodeVisitReported = true;
        }
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


class $modify(OptGodMenuLayer, MenuLayer) {
    void onEnterTransitionDidFinish() {
        MenuLayer::onEnterTransitionDidFinish();
        if (!g_generalEnabled) return;

        // Only attempt once per process. If the level cannot be resolved,
        // leave the normal menu available and let the player choose manually.
        if (g_lastLevelLaunchAttempted) return;
        g_lastLevelLaunchAttempted = true;

        if (!Mod::get()->getSettingValue<bool>("load-last-level")) return;

        auto const id = Mod::get()->getSavedValue<int>("last-level-id", 0);
        auto const isMainLevel = Mod::get()->getSavedValue<bool>("last-level-main", false);
        if (id <= 0) {
            log::info("DashCore: no previous level is stored; keeping the main menu.");
            return;
        }

        auto* manager = GameLevelManager::sharedState();
        auto* level = isMainLevel
            ? manager->getMainLevel(id, false)
            : manager->getSavedLevel(id);

        if (!level) {
            log::warn("DashCore: could not resolve previous level id={}; keeping the main menu.", id);
            return;
        }

        log::info("DashCore: loading previous level '{}' (id={}, main={}).",
            level->m_levelName, id, isMainLevel);
        auto* scene = PlayLayer::scene(level, false, false);
        if (!scene) {
            log::warn("DashCore: PlayLayer scene creation failed for id={}; keeping the main menu.", id);
            return;
        }
        cocos2d::CCDirector::sharedDirector()->replaceScene(
            cocos2d::CCTransitionFade::create(0.5f, scene)
        );
    }
};


class $modify(OptGodDirector, cocos2d::CCDirector) {
    static void onModify(auto& self) {
        auto result = self.getHook("CCDirector::drawScene");
        if (!result) {
            log::error("DashCore 0.9.4-alpha: could not register CCDirector::drawScene diagnostic hook.");
            return;
        }
        g_drawSceneHook = result.unwrap();
        g_drawSceneHook->setAutoEnable(false);
    }

    void drawScene() {
        if (!g_generalEnabled || !g_diagnosticsEnabled) {
            CCDirector::drawScene();
            return;
        }
        bool const profiling = g_profile.layer != nullptr &&
            g_profile.drawFrames < kFramesToMeasure;
        auto const start = Clock::now();



        if (profiling && g_profile.previousDrawScene.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    start - g_profile.previousDrawScene).count());
            g_profile.maxDrawGapUs = std::max(g_profile.maxDrawGapUs, gapUs);
            addGap(g_profile.drawGapBuckets, gapUs);
            g_profile.drawGapSamples.push_back(gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.drawGapsOver50ms;
            if (gapUs > kSevereFrameGapUs) ++g_profile.drawGapsOver100ms;
        }

        CCDirector::drawScene();
        // Do not run the compute probe inside a measured DrawScene interval.
        // Wait until the 600-frame profile and its object diagnostics are complete.
        if (!profiling) {
            if (g_gpuObjectProbeStarted) {
                // GL context is active here, outside the measured gameplay window.
                // The probe restores the GL state it touches and never replaces GD draws.
                dashcore::bismuth::runGLESComputeProbeOnce();
            }
            return;
        }

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
            // Refresh the scene snapshot at the exact end of the 600-frame
            // window. Geometry Dash may continue populating object nodes
            // during early gameplay, so older snapshots are not comparable.
            auto const scanStart = Clock::now();
            auto const finalScan = inspectSceneGraph(g_profile.layer);
            g_profile.sceneScanUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - scanStart).count());
            ++g_profile.sceneScans;
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
        // Wait until the full 600-frame sample has completed before collecting
        // GPU inputs. The previous build collected on frame 1, before the
        // level's object graph had finished populating (only 2 objects seen).
        // Use the exact same PlayLayer root as inspectSceneGraph and compare
        // both traversals before running the read-only GLES probe.
        if (g_profile.layer && g_profile.drawFrames == kFramesToMeasure &&
            !g_gpuObjectProbeStarted) {
            g_gpuObjectProbeStarted = true;
            auto* root = static_cast<CCNode*>(g_profile.layer);
            // Reuse the final scene snapshot instead of traversing the same
            // PlayLayer root a second time. This keeps the diagnostic pass bounded.
            SceneScan scan;
            scan.nodes = g_profile.sceneNodes;
            scan.gameObjects = g_profile.sceneGameObjects;
            scan.effectObjects = g_profile.sceneEffectObjects;
            scan.groupedObjects = g_profile.sceneGroupedObjects;
            scan.hiddenGameObjects = g_profile.sceneHiddenGameObjects;
            scan.maxChildren = g_profile.sceneMaxChildren;
            scan.maxDepth = g_profile.sceneMaxDepth;
            auto const scanUs = g_profile.sceneScanUs;

            auto const collectStart = Clock::now();
            auto const inputs = collectGpuCullInputs(root);
            auto const collectUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - collectStart).count();

            auto* director = cocos2d::CCDirector::sharedDirector();
            auto const screen = director->getWinSize();
            auto const visibleSize = director->getVisibleSize();
            auto const visibleOrigin = director->getVisibleOrigin();
            log::info(
                "DashCore 0.9.4-alpha VIEWPORT-SPACE: winSize=({:.2f},{:.2f}), visibleOrigin=({:.2f},{:.2f}), visibleSize=({:.2f},{:.2f}), contentScaleFactor={:.3f}. World-space object bounds are diagnostic only; camera translation/zoom is not yet applied to the culling candidate.",
                screen.width, screen.height, visibleOrigin.x, visibleOrigin.y,
                visibleSize.width, visibleSize.height, director->getContentScaleFactor()
            );
            auto const boundsStart = Clock::now();
            auto const bounds = validateObjectBounds(root, screen.width, screen.height);
            auto const boundsUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - boundsStart).count();

            auto const geometryStart = Clock::now();
            auto const geometry = inspectObjectGeometry(root);
            auto const geometryUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - geometryStart).count();

            log::info(
                "DashCore 0.9.4-alpha DIAGNOSTIC-COST: sceneScan={}us, gpuInputCollection={}us, boundsValidation={}us, objectGeometry={}us, gpuInputs={}, boundsObjects={}.",
                scanUs, collectUs, boundsUs, geometryUs, inputs.size(), bounds.scannedGameObjects
            );
            log::info(
                "DashCore 0.9.4-alpha BISMUTH-CULL: same-frame traversal check; boundsSource=node-local-content-world, groupedPolicy=force-visible; rootChildren={}, scannedNodes={}, scannedGameObjects={}, collectedGameObjects={}, boundsScannedNodes={}, boundsScannedObjects={}, geometryScannedNodes={}, geometryObjects={}, traversalDelta={}.",
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
    g_generalEnabled = Mod::get()->getSettingValue<bool>("general-enable");
    g_diagnosticsEnabled = Mod::get()->getSettingValue<bool>("diagnostics-enable");
    log::info("DashCore 0.9.4-alpha GENERAL-ENABLE: {}.", g_generalEnabled ? "ON" : "OFF");
    log::info("DashCore 0.9.4-alpha PERFORMANCE-DIAGNOSTICS: {}.",
        g_diagnosticsEnabled ? "ON" : "OFF");
    // AutoTest can compare the same binary in ON/OFF mode after a full restart.
    // Missing or unrecognized files keep the production default enabled.
    auto const modePath = Mod::get()->getConfigDir(true) / "fast-path-mode.txt";
    std::ifstream modeFile(modePath.string());
    std::string requestedMode;
    bool const hasModeOverride = static_cast<bool>(modeFile >> requestedMode);
    if (hasModeOverride) {
        if (requestedMode == "off" || requestedMode == "0") {
            g_fastPathEnabled = false;
        } else if (requestedMode == "on" || requestedMode == "1") {
            g_fastPathEnabled = true;
        } else {
            log::warn("DashCore 0.9.4-alpha FAST-PATH: unknown mode '{}'; keeping default ON.", requestedMode);
        }
    }
    log::info(
        "DashCore 0.9.4-alpha FAST-PATH: mode={}, source={}, config={}.",
        g_fastPathEnabled ? "ON" : "OFF",
        hasModeOverride ? "file" : "default",
        modePath.string()
    );
    if (g_generalEnabled && g_diagnosticsEnabled) {
        enableDiagnosticHook(g_nodeVisitHook, "CCNode::visit");
        enableDiagnosticHook(g_postUpdateHook, "PlayLayer::postUpdate");
        enableDiagnosticHook(g_drawSceneHook, "CCDirector::drawScene");
    } else {
        log::info("DashCore 0.9.4-alpha: high-frequency diagnostic hooks remain disabled; normal gameplay uses the original Cocos paths.");
    }
    log::info(
        "DashCore 0.9.4-alpha: diagnostics are opt-in; live render culling remains hard-disabled."
    );
}
