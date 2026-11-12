#include <Geode/Geode.hpp>
#include "bismuth/GLESComputeProbe.hpp"
#include "bismuth/GLESObjectCullingProbe.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCSprite.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCTextureAtlas.hpp>
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
#include <string_view>

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
        std::uint64_t protectedPlayerObjects = 0;
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

    constexpr std::uint32_t kRenderPipelineSampleFrames = 180;
    constexpr std::uint32_t kRenderPipelineTimingStride = 8;

    using TimingBuckets = std::array<std::uint32_t, 7>;

    void recordTimingBucket(TimingBuckets& buckets, std::int64_t elapsedUs) {
        if (elapsedUs <= 250) ++buckets[0];
        else if (elapsedUs <= 500) ++buckets[1];
        else if (elapsedUs <= 1000) ++buckets[2];
        else if (elapsedUs <= 2000) ++buckets[3];
        else if (elapsedUs <= 4000) ++buckets[4];
        else if (elapsedUs <= 8000) ++buckets[5];
        else ++buckets[6];
    }

    void reportTimingBuckets(char const* callback, TimingBuckets const& buckets) {
        std::uint32_t samples = 0;
        for (auto const count : buckets) samples += count;
        log::info(
            "DashCore 0.10.12-alpha RENDER-TIMING-BINS: callback={}, samples={}, us0_250={}, us251_500={}, us501_1000={}, us1001_2000={}, us2001_4000={}, us4001_8000={}, usOver8000={}.",
            callback, samples, buckets[0], buckets[1], buckets[2], buckets[3],
            buckets[4], buckets[5], buckets[6]
        );
    }

    struct RenderCallbackMetric {
        std::uint64_t calls = 0;
        std::uint64_t timedCalls = 0;
        std::int64_t inclusiveTotalUs = 0;
        std::int64_t inclusiveMaxUs = 0;
        TimingBuckets inclusiveDurationBuckets{};
    };

    struct RenderPipelineProfile {
        bool active = false;
        bool finalReported = false;
        bool sampleThisFrame = false;
        std::uint32_t frames = 0;
        std::uint32_t timedDrawFrames = 0;
        std::int64_t drawSceneTotalUs = 0;
        std::int64_t drawSceneMaxUs = 0;
        TimingBuckets drawSceneDurationBuckets{};
        std::uint32_t updateFrames = 0;
        std::uint32_t slowPostUpdates = 0;
        std::int64_t postUpdateTotalUs = 0;
        std::int64_t postUpdateMaxUs = 0;
        TimingBuckets postUpdateDurationBuckets{};
        float minUpdateDt = std::numeric_limits<float>::max();
        float maxUpdateDt = 0.f;
        RenderCallbackMetric nodeVisit;
        RenderCallbackMetric spriteDraw;
        RenderCallbackMetric batchDraw;
        RenderCallbackMetric atlasDrawQuads;
        RenderCallbackMetric atlasDrawNumberOfQuads;
        RenderCallbackMetric baseGameLayerUpdate;
        RenderCallbackMetric playLayerUpdateVisibility;
    };

    class RenderCallScope {
        RenderCallbackMetric* m_metric = nullptr;
        bool m_timed = false;
        Clock::time_point m_start{};
    public:
        RenderCallScope(RenderCallbackMetric& metric, bool active, bool timed)
            : m_metric(active ? &metric : nullptr), m_timed(active && timed) {
            if (!m_metric) return;
            ++m_metric->calls;
            if (m_timed) {
                ++m_metric->timedCalls;
                m_start = Clock::now();
            }
        }

        ~RenderCallScope() {
            if (!m_metric || !m_timed) return;
            auto const elapsedUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                    Clock::now() - m_start).count());
            m_metric->inclusiveTotalUs += elapsedUs;
            m_metric->inclusiveMaxUs = std::max(m_metric->inclusiveMaxUs, elapsedUs);
            recordTimingBucket(m_metric->inclusiveDurationBuckets, elapsedUs);
        }
    };

    StartupProfile g_profile;
    RenderPipelineProfile g_rendererProfile;
    bool g_renderPipelineDiagnosticsRequested = false;
    bool g_renderPipelineHooksReady = false;
    bool g_baseGameLayerUpdateHookReady = false;
    bool g_playLayerVisibilityHookReady = false;
    bool g_fastPathEnabled = true; // Default production behavior; AutoTest overrides this at startup.
    bool g_generalEnabled = true; // Master switch: keep the mod loaded but bypass all DashCore behavior.
    bool g_diagnosticsEnabled = false; // Expensive profiling and scene/GPU probes are opt-in.
    bool g_cullingEnabled = false;
    bool g_cullingRequested = false;
    bool g_lastEngineInitFlag = false;
    bool g_gpuObjectProbeStarted = false;
    bool g_cullingTelemetryEnabled = false;
    constexpr std::uint64_t kCullingTelemetryWarmupFrames = 600;
    constexpr std::uint64_t kCullingTelemetrySampleFrames = 120;
    std::uint64_t g_cullingWarmupFrames = 0;
    std::uint64_t g_cullingFrames = 0;
    std::uint64_t g_cullingVisitedObjects = 0;
    std::uint64_t g_cullingEligibleObjects = 0;
    std::uint64_t g_cullingCulledObjects = 0;
    bool g_cullingSamplingActive = false;

    // Mutually exclusive per-visit reasons make an inconclusive culling run
    // diagnosable without enabling the much heavier full scene diagnostics.
    struct CullingReasonTelemetry {
        std::uint64_t protectedPlayer = 0;
        std::uint64_t protectedEffect = 0;
        std::uint64_t protectedTrigger = 0;
        std::uint64_t protectedGrouped = 0;
        std::uint64_t protectedRotating = 0;
        std::uint64_t missingDirector = 0;
        std::uint64_t contentSizeFallbacks = 0;
        std::uint64_t invalidBoundingRect = 0;
        std::uint64_t invalidAnchor = 0;
        std::uint64_t invalidWorldBounds = 0;
        std::uint64_t invalidViewport = 0;
        std::uint64_t withinGuardBand = 0;
        std::uint64_t offscreenX = 0;
        std::uint64_t offscreenY = 0;
        std::uint64_t offscreenBoth = 0;
        bool hasNearestCandidate = false;
        float nearestGuardClearance = std::numeric_limits<float>::max();
        int nearestObjectID = -1;
        float nearestMinX = 0.f;
        float nearestMinY = 0.f;
        float nearestMaxX = 0.f;
        float nearestMaxY = 0.f;
    };
    CullingReasonTelemetry g_cullingReasons;

    enum class ProtectedCullingReason {
        Player, Effect, Trigger, Grouped, Rotating
    };

    enum class CullingBoundsReason {
        MissingDirector,
        InvalidBoundingRect,
        InvalidAnchor,
        InvalidWorldBounds,
        InvalidViewport,
        WithinGuardBand,
        OffscreenX,
        OffscreenY,
        OffscreenBoth
    };

    struct CullingBoundsEvaluation {
        CullingBoundsReason reason = CullingBoundsReason::InvalidWorldBounds;
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
        float guardClearance = 0.f;
        bool shouldCull = false;
    };

    void recordProtectedCullingReason(ProtectedCullingReason reason) {
        if (!g_cullingTelemetryEnabled || !g_cullingSamplingActive) return;
        switch (reason) {
            case ProtectedCullingReason::Player: ++g_cullingReasons.protectedPlayer; break;
            case ProtectedCullingReason::Effect: ++g_cullingReasons.protectedEffect; break;
            case ProtectedCullingReason::Trigger: ++g_cullingReasons.protectedTrigger; break;
            case ProtectedCullingReason::Grouped: ++g_cullingReasons.protectedGrouped; break;
            case ProtectedCullingReason::Rotating: ++g_cullingReasons.protectedRotating; break;
        }
    }

    void recordCullingBoundsEvaluation(
        GameObject* object, CullingBoundsEvaluation const& evaluation
    ) {
        if (!g_cullingTelemetryEnabled || !g_cullingSamplingActive) return;
        switch (evaluation.reason) {
            case CullingBoundsReason::MissingDirector: ++g_cullingReasons.missingDirector; break;
            case CullingBoundsReason::InvalidBoundingRect: ++g_cullingReasons.invalidBoundingRect; break;
            case CullingBoundsReason::InvalidAnchor: ++g_cullingReasons.invalidAnchor; break;
            case CullingBoundsReason::InvalidWorldBounds: ++g_cullingReasons.invalidWorldBounds; break;
            case CullingBoundsReason::InvalidViewport: ++g_cullingReasons.invalidViewport; break;
            case CullingBoundsReason::WithinGuardBand: ++g_cullingReasons.withinGuardBand; break;
            case CullingBoundsReason::OffscreenX: ++g_cullingReasons.offscreenX; break;
            case CullingBoundsReason::OffscreenY: ++g_cullingReasons.offscreenY; break;
            case CullingBoundsReason::OffscreenBoth: ++g_cullingReasons.offscreenBoth; break;
        }

        // Track the eligible object closest to becoming fully outside the guard
        // band. This reveals whether a zero-skip run is caused by the margin.
        if (evaluation.reason == CullingBoundsReason::WithinGuardBand &&
            object && std::isfinite(evaluation.guardClearance) &&
            (!g_cullingReasons.hasNearestCandidate ||
             evaluation.guardClearance < g_cullingReasons.nearestGuardClearance)) {
            g_cullingReasons.hasNearestCandidate = true;
            g_cullingReasons.nearestGuardClearance = evaluation.guardClearance;
            g_cullingReasons.nearestObjectID = static_cast<int>(object->m_objectID);
            g_cullingReasons.nearestMinX = evaluation.minX;
            g_cullingReasons.nearestMinY = evaluation.minY;
            g_cullingReasons.nearestMaxX = evaluation.maxX;
            g_cullingReasons.nearestMaxY = evaluation.maxY;
        }
    }

    void advanceCullingTelemetry() {
        if (!g_generalEnabled || !g_cullingEnabled || !g_cullingTelemetryEnabled) return;

        // Let level construction and the initial camera/object layout settle.
        // The first object visits counted below belong to the frame AFTER all
        // 600 warmup updates have completed.
        if (!g_cullingSamplingActive) {
            if (g_cullingWarmupFrames < kCullingTelemetryWarmupFrames) {
                ++g_cullingWarmupFrames;
                return;
            }

            g_cullingSamplingActive = true;
            g_cullingFrames = 0;
            g_cullingVisitedObjects = 0;
            g_cullingEligibleObjects = 0;
            g_cullingCulledObjects = 0;
            g_cullingReasons = {};
            log::info(
                "DashCore 0.10.12-alpha CULL-TELEMETRY: warmup complete after {} update frames; sampling the next {} gameplay frames.",
                g_cullingWarmupFrames, kCullingTelemetrySampleFrames
            );
            return;
        }

        // postUpdate runs before that frame's CCNode::visit traversal. Logging
        // at 120 here therefore closes the 120 visits from the preceding frames.
        ++g_cullingFrames;
        if (g_cullingFrames >= kCullingTelemetrySampleFrames) {
            log::info(
                "DashCore 0.10.12-alpha CULL-STATS frames={}, warmupFrames={}, visitedGameObjects={}, eligibleObjects={}, culledObjects={}.",
                g_cullingFrames, g_cullingWarmupFrames, g_cullingVisitedObjects,
                g_cullingEligibleObjects, g_cullingCulledObjects
            );
            log::info(
                "DashCore 0.10.12-alpha CULL-REASONS visited={}, eligible={}, protectedPlayer={}, protectedEffect={}, protectedTrigger={}, protectedGrouped={}, protectedRotating={}, missingDirector={}, contentSizeFallbacks={}, invalidBoundingRect={}, invalidAnchor={}, invalidWorldBounds={}, invalidViewport={}, withinGuardBand={}, offscreenX={}, offscreenY={}, offscreenBoth={}.",
                g_cullingVisitedObjects, g_cullingEligibleObjects,
                g_cullingReasons.protectedPlayer, g_cullingReasons.protectedEffect, g_cullingReasons.protectedTrigger,
                g_cullingReasons.protectedGrouped, g_cullingReasons.protectedRotating,
                g_cullingReasons.missingDirector, g_cullingReasons.contentSizeFallbacks,
                g_cullingReasons.invalidBoundingRect, g_cullingReasons.invalidAnchor,
                g_cullingReasons.invalidWorldBounds, g_cullingReasons.invalidViewport,
                g_cullingReasons.withinGuardBand, g_cullingReasons.offscreenX,
                g_cullingReasons.offscreenY, g_cullingReasons.offscreenBoth
            );
            log::info(
                "DashCore 0.10.12-alpha CULL-NEAREST: found={}, guardClearance={:.2f}, objectID={}, bounds=({:.2f},{:.2f})-({:.2f},{:.2f}).",
                g_cullingReasons.hasNearestCandidate,
                g_cullingReasons.hasNearestCandidate ? g_cullingReasons.nearestGuardClearance : 0.f,
                g_cullingReasons.nearestObjectID,
                g_cullingReasons.nearestMinX, g_cullingReasons.nearestMinY,
                g_cullingReasons.nearestMaxX, g_cullingReasons.nearestMaxY
            );
            g_cullingTelemetryEnabled = false;
            g_cullingSamplingActive = false;
            log::info(
                "DashCore 0.10.12-alpha CULL-TELEMETRY: the {}-frame sample is complete; per-object counters are now off.",
                kCullingTelemetrySampleFrames
            );
        }
    }

    // High-frequency hooks are registered with auto-enable OFF. They are enabled
    // only when diagnostics or experimental culling explicitly requires them.
    Hook* g_nodeVisitHook = nullptr;
    Hook* g_postUpdateHook = nullptr;
    Hook* g_drawSceneHook = nullptr;
    Hook* g_spriteDrawHook = nullptr;
    Hook* g_batchDrawHook = nullptr;
    Hook* g_atlasDrawQuadsHook = nullptr;
    Hook* g_atlasDrawNumberOfQuadsHook = nullptr;
    Hook* g_baseGameLayerUpdateHook = nullptr;
    Hook* g_playLayerVisibilityHook = nullptr;

    bool enableDiagnosticHook(Hook* hook, char const* name) {
        if (!hook) {
            log::warn("DashCore 0.10.12-alpha: hook '{}' is unavailable; the dependent feature remains disabled.", name);
            return false;
        }
        auto result = hook->enable();
        if (!result) {
            log::error("DashCore 0.10.12-alpha: could not enable hook '{}'.", name);
            return false;
        }
        log::info("DashCore 0.10.12-alpha: hook '{}' enabled.", name);
        return true;
    }

    // Do not guess the generated hook-map key. Geode stores the actual method
    // display name in each Modify instance; Cocos class names may be qualified.
    template <typename Modify>
    Hook* registerOptionalHook(Modify& self, std::string_view methodSuffix, char const* label) {
        auto const hooks = self.getAllHooks();
        for (auto* hook : hooks) {
            if (hook && hook->getDisplayName().find(methodSuffix) != std::string_view::npos) {
                hook->setAutoEnable(false);
                log::info(
                    "DashCore 0.10.12-alpha: registered hook '{}' with auto-enable OFF.",
                    hook->getDisplayName()
                );
                return hook;
            }
        }

        std::string available;
        for (auto* hook : hooks) {
            if (!hook) continue;
            if (!available.empty()) available += ", ";
            available += hook->getDisplayName();
        }
        log::warn(
            "DashCore 0.10.12-alpha: optional hook '{}' is absent from the modify registry; available hooks=[{}]. Dependent features will remain unavailable.",
            label, available
        );
        return nullptr;
    }

    // Bismuth-inspired conservative CPU bounds test. The original 125% guard
    // band and all fail-open cases are preserved; this version only exposes the
    // exact reason each eligible GameObject is or is not skipped.
    CullingBoundsEvaluation evaluateCullBounds(GameObject* object) {
        CullingBoundsEvaluation result;
        auto* director = cocos2d::CCDirector::sharedDirector();
        if (!director) {
            result.reason = CullingBoundsReason::MissingDirector;
            return result;
        }

        auto const size = object->getContentSize();
        bool const validContentSize =
            std::isfinite(size.width) && std::isfinite(size.height) &&
            size.width > 0.f && size.height > 0.f;

        if (validContentSize) {
            auto const anchor = object->getAnchorPoint();
            if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y)) {
                result.reason = CullingBoundsReason::InvalidAnchor;
                return result;
            }

            auto const x1 = -anchor.x * size.width;
            auto const y1 = -anchor.y * size.height;
            auto const x2 = x1 + size.width;
            auto const y2 = y1 + size.height;

            // Transform all four corners through the object's complete node chain.
            auto const p1 = object->convertToWorldSpace({ x1, y1 });
            auto const p2 = object->convertToWorldSpace({ x2, y1 });
            auto const p3 = object->convertToWorldSpace({ x1, y2 });
            auto const p4 = object->convertToWorldSpace({ x2, y2 });

            result.minX = std::min({ p1.x, p2.x, p3.x, p4.x });
            result.maxX = std::max({ p1.x, p2.x, p3.x, p4.x });
            result.minY = std::min({ p1.y, p2.y, p3.y, p4.y });
            result.maxY = std::max({ p1.y, p2.y, p3.y, p4.y });
        } else {
            // Geometry Dash GameObjects can expose an empty CCNode content size
            // while their game-specific bounding rect is populated. The rect is
            // parent-relative (like CCNode::getBoundingBox), so transform its
            // four corners through the parent. If it is unusable, fail open.
            auto const rect = object->getBoundingRect();
            auto* parent = object->getParent();
            if (!parent || !std::isfinite(rect.origin.x) || !std::isfinite(rect.origin.y) ||
                !std::isfinite(rect.size.width) || !std::isfinite(rect.size.height) ||
                rect.size.width <= 0.f || rect.size.height <= 0.f) {
                result.reason = CullingBoundsReason::InvalidBoundingRect;
                return result;
            }

            if (g_cullingTelemetryEnabled && g_cullingSamplingActive) ++g_cullingReasons.contentSizeFallbacks;
            auto const x1 = rect.origin.x;
            auto const y1 = rect.origin.y;
            auto const x2 = x1 + rect.size.width;
            auto const y2 = y1 + rect.size.height;
            auto const p1 = parent->convertToWorldSpace({ x1, y1 });
            auto const p2 = parent->convertToWorldSpace({ x2, y1 });
            auto const p3 = parent->convertToWorldSpace({ x1, y2 });
            auto const p4 = parent->convertToWorldSpace({ x2, y2 });

            result.minX = std::min({ p1.x, p2.x, p3.x, p4.x });
            result.maxX = std::max({ p1.x, p2.x, p3.x, p4.x });
            result.minY = std::min({ p1.y, p2.y, p3.y, p4.y });
            result.maxY = std::max({ p1.y, p2.y, p3.y, p4.y });
        }
        if (!std::isfinite(result.minX) || !std::isfinite(result.maxX) ||
            !std::isfinite(result.minY) || !std::isfinite(result.maxY) ||
            result.minX > result.maxX || result.minY > result.maxY) {
            result.reason = CullingBoundsReason::InvalidWorldBounds;
            return result;
        }

        auto const origin = director->getVisibleOrigin();
        auto const viewport = director->getVisibleSize();
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
            !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
            viewport.width <= 0.f || viewport.height <= 0.f) {
            result.reason = CullingBoundsReason::InvalidViewport;
            return result;
        }

        // Keep the exact existing 125% guard band; no threshold is relaxed.
        auto const marginX = viewport.width * 1.25f;
        auto const marginY = viewport.height * 1.25f;
        auto const viewMinX = origin.x - marginX;
        auto const viewMinY = origin.y - marginY;
        auto const viewMaxX = origin.x + viewport.width + marginX;
        auto const viewMaxY = origin.y + viewport.height + marginY;

        // Positive clearance means the object remains inside the guard band.
        // Negative clearance identifies how far it has crossed the nearest edge.
        result.guardClearance = std::min({
            result.maxX - viewMinX, viewMaxX - result.minX,
            result.maxY - viewMinY, viewMaxY - result.minY
        });
        bool const outsideX = result.maxX < viewMinX || result.minX > viewMaxX;
        bool const outsideY = result.maxY < viewMinY || result.minY > viewMaxY;
        result.shouldCull = outsideX || outsideY;
        if (outsideX && outsideY) result.reason = CullingBoundsReason::OffscreenBoth;
        else if (outsideX) result.reason = CullingBoundsReason::OffscreenX;
        else if (outsideY) result.reason = CullingBoundsReason::OffscreenY;
        else result.reason = CullingBoundsReason::WithinGuardBand;
        return result;
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
        float width, float height, float originX = 0.f, float originY = 0.f
    ) {
        if (!std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY) ||
            minX > maxX || minY > maxY) return false;
        auto const marginX = width * 0.75f;
        auto const marginY = height * 0.75f;
        return maxX >= originX - marginX && minX <= originX + width + marginX &&
               maxY >= originY - marginY && minY <= originY + height + marginY;
    }

    void recordBoundsCandidate(
        BoundsCandidateStats& stats,
        float minX, float minY, float maxX, float maxY,
        float width, float height, float originX = 0.f, float originY = 0.f
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
        if (intersectsExpandedViewport(minX, minY, maxX, maxY, width, height, originX, originY)) {
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

    BoundsValidation validateObjectArrayBounds(
        PlayLayer* layer, float width, float height, float originX, float originY
    ) {
        BoundsValidation result;
        if (!layer || !layer->m_objects || width <= 0.f || height <= 0.f) return result;
        result.scannedNodes = layer->m_objects->count();

        for (auto object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            if (!object) continue;
            ++result.scannedGameObjects;
            auto const rect = object->getBoundingRect();
            auto* parent = object->getParent();
            bool const rectValid = std::isfinite(rect.origin.x) && std::isfinite(rect.origin.y) &&
                std::isfinite(rect.size.width) && std::isfinite(rect.size.height) &&
                rect.size.width > 0.f && rect.size.height > 0.f;
            if (parent && rectValid) {
                auto const x1=rect.origin.x, y1=rect.origin.y;
                auto const x2=x1+rect.size.width, y2=y1+rect.size.height;
                auto const p1=parent->convertToWorldSpace({x1,y1});
                auto const p2=parent->convertToWorldSpace({x2,y1});
                auto const p3=parent->convertToWorldSpace({x1,y2});
                auto const p4=parent->convertToWorldSpace({x2,y2});
                recordBoundsCandidate(result.parentTransformed,
                    std::min({p1.x,p2.x,p3.x,p4.x}), std::min({p1.y,p2.y,p3.y,p4.y}),
                    std::max({p1.x,p2.x,p3.x,p4.x}), std::max({p1.y,p2.y,p3.y,p4.y}),
                    width,height,originX,originY);
            } else {
                ++result.parentTransformed.objects; ++result.parentTransformed.invalid;
            }
            recordBoundsCandidate(result.rawRect,rect.getMinX(),rect.getMinY(),rect.getMaxX(),rect.getMaxY(),
                width,height,originX,originY);

            auto const size=object->getContentSize();
            auto const anchor=object->getAnchorPoint();
            bool const contentValid=std::isfinite(size.width)&&std::isfinite(size.height)&&
                size.width>0.f&&size.height>0.f&&std::isfinite(anchor.x)&&std::isfinite(anchor.y);
            if (contentValid) {
                auto const x1=-anchor.x*size.width, y1=-anchor.y*size.height;
                auto const x2=x1+size.width, y2=y1+size.height;
                auto const p1=object->convertToWorldSpace({x1,y1});
                auto const p2=object->convertToWorldSpace({x2,y1});
                auto const p3=object->convertToWorldSpace({x1,y2});
                auto const p4=object->convertToWorldSpace({x2,y2});
                recordBoundsCandidate(result.nodeLocal,
                    std::min({p1.x,p2.x,p3.x,p4.x}),std::min({p1.y,p2.y,p3.y,p4.y}),
                    std::max({p1.x,p2.x,p3.x,p4.x}),std::max({p1.y,p2.y,p3.y,p4.y}),
                    width,height,originX,originY);
            } else {
                ++result.nodeLocal.objects; ++result.nodeLocal.invalid;
            }
        }
        return result;
    }

    void reportBoundsCandidate(char const* name, BoundsCandidateStats const& s) {
        log::info(
            "DashCore 0.10.12-alpha BOUNDS {}: objects={}, valid={}, invalid={}, visibleWithMargin={}, offscreenWithMargin={}, extents=({:.1f},{:.1f})-({:.1f},{:.1f}).",
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
        GameObject* nearestSafeCandidate = nullptr;
        CullingBoundsEvaluation nearestSafeEvaluation;
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

                // Read-only candidate selection mirrors the visit hook's exact
                // eligibility checks. It does not alter node state or drawing.
                bool const cullingEligible =
                    typeinfo_cast<PlayerObject*>(object) == nullptr &&
                    object->m_classType != GameObjectClassType::Effect &&
                    !object->isTrigger() && object->m_groupCount <= 0 &&
                    !object->getHasRotateAction();
                if (cullingEligible) {
                    auto const candidateBounds = evaluateCullBounds(object);
                    if (candidateBounds.reason == CullingBoundsReason::WithinGuardBand &&
                        (!nearestSafeCandidate ||
                         candidateBounds.guardClearance < nearestSafeEvaluation.guardClearance)) {
                        nearestSafeCandidate = object;
                        nearestSafeEvaluation = candidateBounds;
                    }
                }
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
                    log::info("DashCore 0.10.12-alpha TRIGGER-SAMPLE {}: classType={}, groups={}, parent={}, pos=({:.2f},{:.2f}), world=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), scale=({:.3f},{:.3f}), rotation={:.2f}.",
                        stats.triggerSamples, static_cast<int>(object->m_classType), object->m_groupCount,
                        sampleParent != nullptr, samplePos.x, samplePos.y, sampleWorld.x, sampleWorld.y,
                        rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
                        object->getScaleX(), object->getScaleY(), object->getRotation());
                    ++stats.triggerSamples;
                }
                if (object->m_groupCount > 0 && stats.groupedSamples < 8) {
                    log::info("DashCore 0.10.12-alpha GROUPED-SAMPLE {}: trigger={}, classType={}, groups={}, parent={}, pos=({:.2f},{:.2f}), world=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), scale=({:.3f},{:.3f}), rotation={:.2f}.",
                        stats.groupedSamples, object->isTrigger(), static_cast<int>(object->m_classType),
                        object->m_groupCount, sampleParent != nullptr, samplePos.x, samplePos.y,
                        sampleWorld.x, sampleWorld.y, rect.origin.x, rect.origin.y, rect.size.width,
                        rect.size.height, object->getScaleX(), object->getScaleY(), object->getRotation());
                    ++stats.groupedSamples;
                }
                // Sample IDs only after the native trigger predicate identifies a trigger;
                // this avoids spending the bounded sample on unrelated early objects.
                if (object->isTrigger() && objectIdSamples < 24) {
                    log::info("DashCore 0.10.12-alpha TRIGGER-ID-SAMPLE {}: objectID={}, isTrigger={}, classType={}, groups={}, effect={}.",
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
                        "DashCore 0.10.12-alpha OBJECT-SAMPLE {}: objectID={}, trigger={}, classType={}, groups={}, parent={}, pos=({:.2f},{:.2f}), worldAnchor=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), objectScale=({:.3f},{:.3f}), objectRotation={:.2f}, parentScale=({:.3f},{:.3f}), parentRotation={:.2f}.",
                        detailedSamples, objectID, object->isTrigger(),
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
        if (nearestSafeCandidate) {
            auto const rect = nearestSafeCandidate->getBoundingRect();
            auto const size = nearestSafeCandidate->getContentSize();
            auto const anchor = nearestSafeCandidate->getAnchorPoint();
            auto const position = nearestSafeCandidate->getPosition();
            auto const scaleX = nearestSafeCandidate->getScaleX();
            auto const scaleY = nearestSafeCandidate->getScaleY();
            auto const rotation = nearestSafeCandidate->getRotation();
            auto* parent = nearestSafeCandidate->getParent();
            auto const parentWorldAnchor = parent ? parent->convertToWorldSpace(position) : position;
            auto const nodeWorldOrigin = nearestSafeCandidate->convertToWorldSpace({0.f, 0.f});
            auto const rectFinite =
                std::isfinite(rect.origin.x) && std::isfinite(rect.origin.y) &&
                std::isfinite(rect.size.width) && std::isfinite(rect.size.height);
            auto* director = cocos2d::CCDirector::sharedDirector();
            auto const viewportOrigin = director->getVisibleOrigin();
            auto const viewportSize = director->getVisibleSize();

            log::info(
                "DashCore 0.10.12-alpha CULL-GEOMETRY-CANDIDATE: objectID={}, classType={}, trigger={}, groups={}, rotateAction={}, reason={}, guardClearance={:.2f}, worldBounds=({:.2f},{:.2f})-({:.2f},{:.2f}), contentSize=({:.2f},{:.2f}), anchor=({:.3f},{:.3f}), position=({:.2f},{:.2f}), parentWorldAnchor=({:.2f},{:.2f}), nodeWorldOrigin=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), rectFinite={}, objectScale=({:.3f},{:.3f}), objectRotation={:.2f}, parentPresent={}.",
                static_cast<int>(nearestSafeCandidate->m_objectID),
                static_cast<int>(nearestSafeCandidate->m_classType),
                nearestSafeCandidate->isTrigger(), nearestSafeCandidate->m_groupCount,
                nearestSafeCandidate->getHasRotateAction(),
                static_cast<int>(nearestSafeEvaluation.reason),
                nearestSafeEvaluation.guardClearance,
                nearestSafeEvaluation.minX, nearestSafeEvaluation.minY,
                nearestSafeEvaluation.maxX, nearestSafeEvaluation.maxY,
                size.width, size.height, anchor.x, anchor.y,
                position.x, position.y, parentWorldAnchor.x, parentWorldAnchor.y,
                nodeWorldOrigin.x, nodeWorldOrigin.y,
                rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
                rectFinite, scaleX, scaleY, rotation, parent != nullptr
            );
            log::info(
                "DashCore 0.10.12-alpha CULL-GEOMETRY-VIEWPORT: visibleOrigin=({:.2f},{:.2f}), visibleSize=({:.2f},{:.2f}), guardMargin=({:.2f},{:.2f}), winSize=({:.2f},{:.2f}).",
                viewportOrigin.x, viewportOrigin.y, viewportSize.width, viewportSize.height,
                viewportSize.width * 1.25f, viewportSize.height * 1.25f,
                screen.width, screen.height
            );
            if (parent && rectFinite && rect.size.width > 0.f && rect.size.height > 0.f) {
                auto const x1 = rect.origin.x;
                auto const y1 = rect.origin.y;
                auto const x2 = x1 + rect.size.width;
                auto const y2 = y1 + rect.size.height;
                auto const p1 = parent->convertToWorldSpace({x1, y1});
                auto const p2 = parent->convertToWorldSpace({x2, y1});
                auto const p3 = parent->convertToWorldSpace({x1, y2});
                auto const p4 = parent->convertToWorldSpace({x2, y2});
                log::info(
                    "DashCore 0.10.12-alpha CULL-GEOMETRY-RECT-CORNERS: parentSpace=({:.2f},{:.2f})-({:.2f},{:.2f}), worldP1=({:.2f},{:.2f}), worldP2=({:.2f},{:.2f}), worldP3=({:.2f},{:.2f}), worldP4=({:.2f},{:.2f}).",
                    x1, y1, x2, y2, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4.x, p4.y
                );
            } else {
                log::warn("DashCore 0.10.12-alpha CULL-GEOMETRY-RECT-CORNERS: unavailable; parent or bounding rectangle is invalid.");
            }
            auto* chain = static_cast<CCNode*>(nearestSafeCandidate);
            for (int depth = 0; chain && depth < 9; ++depth, chain = chain->getParent()) {
                auto const chainPosition = chain->getPosition();
                auto const chainAnchor = chain->getAnchorPoint();
                auto const chainSize = chain->getContentSize();
                auto const chainWorldOrigin = chain->convertToWorldSpace({0.f, 0.f});
                auto* chainObject = typeinfo_cast<GameObject*>(chain);
                log::info(
                    "DashCore 0.10.12-alpha CULL-GEOMETRY-PARENT: depth={}, isGameObject={}, objectID={}, classType={}, visible={}, position=({:.2f},{:.2f}), anchor=({:.3f},{:.3f}), contentSize=({:.2f},{:.2f}), scale=({:.3f},{:.3f}), rotation={:.2f}, worldOrigin=({:.2f},{:.2f}).",
                    depth, chainObject != nullptr,
                    chainObject ? static_cast<int>(chainObject->m_objectID) : -1,
                    chainObject ? static_cast<int>(chainObject->m_classType) : -1,
                    chain->isVisible(), chainPosition.x, chainPosition.y,
                    chainAnchor.x, chainAnchor.y, chainSize.width, chainSize.height,
                    chain->getScaleX(), chain->getScaleY(), chain->getRotation(),
                    chainWorldOrigin.x, chainWorldOrigin.y
                );
            }
        } else {
            log::warn("DashCore 0.10.12-alpha CULL-GEOMETRY-CANDIDATE: no eligible GameObject remained inside the configured guard band.");
        }
        return stats;
    }

    void reportObjectGeometry(ObjectGeometryStats const& s) {
        log::info(
            "DashCore 0.10.12-alpha OBJECT-GEOMETRY: scannedNodes={}, objects={}, triggers={}, triggerSamples={}, effects={}, grouped={}, groupedSamples={}, ungrouped={}, groupCountRange={}-{}, parentless={}, selfHidden={}, ancestorHidden={}, invalidRect={}, zeroRect={}, negativeRect={}, invalidContent={}, zeroContent={}, scaled={}, rotated={}, maxAbsRotation={:.2f}, scaleMagnitudeRange={:.3f}-{:.3f}, rawRectCandidateOffscreen(trigger/effect/grouped/ordinary)={}/{}/{}/{}, centerComparisons={}, centerDeltaAvg={:.3f}, centerDeltaMax={:.3f}.",
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
            log::info("DashCore 0.10.12-alpha OBJECT-ID: objectID={}, objects={}, isTriggerTrue={}",
                objectID, count, triggerCount);
        }
        for (auto const& [type, count] : s.classTypeCounts) {
            auto const triggerIt = s.triggerClassTypeCounts.find(type);
            auto const triggerCount = triggerIt == s.triggerClassTypeCounts.end() ? 0u : triggerIt->second;
            log::info("DashCore 0.10.12-alpha OBJECT-TYPE: classType={}, objects={}, isTriggerTrue={}.",
                type, count, triggerCount);
        }
        if (s.triggers == 0) {
            log::info("DashCore 0.10.12-alpha TRIGGER-DIAGNOSTIC: isTrigger() returned false for every GameObject in this snapshot; this does not prove the level has no trigger objects.");
        }
    }

    struct ObjectArrayStats {
        std::uint64_t entries=0, objects=0, nullEntries=0, players=0, triggers=0, effects=0;
        std::uint64_t grouped=0, rotating=0, parentless=0, hidden=0, eligible=0, invalidBounds=0;
        std::uint64_t withinGuardBand=0, offscreenX=0, offscreenY=0, offscreenBoth=0;
    };

    ObjectArrayStats inspectPlayLayerObjectArray(PlayLayer* layer) {
        ObjectArrayStats stats;
        if (!layer || !layer->m_objects) {
            log::warn("DashCore 0.10.12-alpha OBJECT-ARRAY: available=false, entries=0, objects=0; PlayLayer::m_objects is null.");
            log::info("DashCore 0.10.12-alpha OBJECT-ARRAY-SAMPLE: emitted=0, cap=12, reason=array-unavailable.");
            return stats;
        }
        stats.entries=layer->m_objects->count();
        std::uint64_t index=0;
        std::uint32_t emittedSamples=0;
        for (auto object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            auto const currentIndex=index++;
            if (!object) { ++stats.nullEntries; continue; }
            ++stats.objects;
            bool const isPlayer=typeinfo_cast<PlayerObject*>(object)!=nullptr;
            bool const isTrigger=object->isTrigger();
            bool const isEffect=object->m_classType==GameObjectClassType::Effect;
            bool const isGrouped=object->m_groupCount>0;
            bool const isRotating=object->getHasRotateAction();
            if(isPlayer)++stats.players;
            if(isTrigger)++stats.triggers;
            if(isEffect)++stats.effects;
            if(isGrouped)++stats.grouped;
            if(isRotating)++stats.rotating;
            if(!object->getParent())++stats.parentless;
            if(!object->isVisible())++stats.hidden;
            bool const eligible=!isPlayer&&!isTrigger&&!isEffect&&!isGrouped&&!isRotating;
            CullingBoundsEvaluation bounds;
            if(eligible) {
                ++stats.eligible;
                bounds=evaluateCullBounds(object);
                switch(bounds.reason) {
                    case CullingBoundsReason::WithinGuardBand: ++stats.withinGuardBand; break;
                    case CullingBoundsReason::OffscreenX: ++stats.offscreenX; break;
                    case CullingBoundsReason::OffscreenY: ++stats.offscreenY; break;
                    case CullingBoundsReason::OffscreenBoth: ++stats.offscreenBoth; break;
                    default: ++stats.invalidBounds; break;
                }
            } else if(emittedSamples<12) bounds=evaluateCullBounds(object);

            if(emittedSamples<12) {
                auto* parent=object->getParent();
                auto const pos=object->getPosition();
                auto const worldAnchor=parent?parent->convertToWorldSpace(pos):pos;
                auto const rect=object->getBoundingRect();
                auto const size=object->getContentSize();
                log::info(
                    "DashCore 0.10.12-alpha OBJECT-ARRAY-SAMPLE: index={}, uniqueID={}, objectID={}, classType={}, player={}, trigger={}, effect={}, grouped={}, rotating={}, eligible={}, parent={}, visible={}, position=({:.2f},{:.2f}), worldAnchor=({:.2f},{:.2f}), rect=({:.2f},{:.2f},{:.2f},{:.2f}), contentSize=({:.2f},{:.2f}), boundsReason={}, guardClearance={:.2f}, worldBounds=({:.2f},{:.2f})-({:.2f},{:.2f}).",
                    currentIndex,object->m_uniqueID,static_cast<int>(object->m_objectID),static_cast<int>(object->m_classType),
                    isPlayer,isTrigger,isEffect,object->m_groupCount,isRotating,eligible,parent!=nullptr,object->isVisible(),
                    pos.x,pos.y,worldAnchor.x,worldAnchor.y,rect.origin.x,rect.origin.y,rect.size.width,rect.size.height,
                    size.width,size.height,static_cast<int>(bounds.reason),bounds.guardClearance,
                    bounds.minX,bounds.minY,bounds.maxX,bounds.maxY);
                ++emittedSamples;
            }
        }
        log::info(
            "DashCore 0.10.12-alpha OBJECT-ARRAY: available=true, entries={}, objects={}, nullEntries={}, players={}, triggers={}, effects={}, grouped={}, rotating={}, parentless={}, hidden={}, eligible={}, invalidBounds={}, withinGuardBand={}, offscreenX={}, offscreenY={}, offscreenBoth={}.",
            stats.entries,stats.objects,stats.nullEntries,stats.players,stats.triggers,stats.effects,stats.grouped,
            stats.rotating,stats.parentless,stats.hidden,stats.eligible,stats.invalidBounds,stats.withinGuardBand,
            stats.offscreenX,stats.offscreenY,stats.offscreenBoth);
        log::info("DashCore 0.10.12-alpha OBJECT-ARRAY-SAMPLE: emitted={}, cap=12.",emittedSamples);
        return stats;
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
    std::vector<dashcore::bismuth::GpuCullInput> collectGpuCullInputs(PlayLayer* layer) {
        std::vector<dashcore::bismuth::GpuCullInput> result;
        if(!layer||!layer->m_objects) {
            log::warn("DashCore 0.10.12-alpha GPU-INPUT-CLASSIFICATION: PlayLayer::m_objects unavailable; no GPU inputs collected.");
            return result;
        }
        std::uint64_t playerCount=0,triggerCount=0,effectCount=0,groupedCount=0;
        std::uint64_t groupedNearCount=0,groupedFarCount=0,rotatingCount=0,parentlessCount=0,invalidBoundsCount=0;
        for(auto object:CCArrayExt<GameObject,false>(layer->m_objects)) {
            if(!object)continue;
            dashcore::bismuth::GpuCullInput input;
            bool const isPlayer=typeinfo_cast<PlayerObject*>(object)!=nullptr;
            bool const isTrigger=object->isTrigger();
            bool const isEffect=object->m_classType==GameObjectClassType::Effect;
            bool const isGrouped=object->m_groupCount>0;
            bool const isRotating=object->getHasRotateAction();
            bool const groupedNear=isGrouped&&isGroupedObjectNearPlayer(object);
            bool const protectedObject=isPlayer||isEffect||isTrigger||isGrouped||isRotating;
            if(isPlayer){++playerCount;input.flags|=(1u<<7);}
            if(isTrigger){++triggerCount;input.flags|=(1u<<1);}
            if(isEffect){++effectCount;input.flags|=(1u<<2);}
            if(isGrouped){++groupedCount;input.flags|=(1u<<3);if(groupedNear){++groupedNearCount;input.flags|=(1u<<4);}else ++groupedFarCount;}
            if(isRotating){++rotatingCount;input.flags|=(1u<<8);}
            if(!object->getParent()){++parentlessCount;input.flags|=(1u<<5);}
            auto const bounds=evaluateCullBounds(object);
            bool const validBounds=bounds.reason==CullingBoundsReason::WithinGuardBand||
                bounds.reason==CullingBoundsReason::OffscreenX||bounds.reason==CullingBoundsReason::OffscreenY||
                bounds.reason==CullingBoundsReason::OffscreenBoth;
            if(validBounds){input.minX=bounds.minX;input.minY=bounds.minY;input.maxX=bounds.maxX;input.maxY=bounds.maxY;}
            else{++invalidBoundsCount;input.flags|=(1u<<6);}
            if(protectedObject||!validBounds)input.flags|=1u;
            result.push_back(input);
        }
        log::info(
            "DashCore 0.10.12-alpha GPU-INPUT-CLASSIFICATION: source=PlayLayer::m_objects+evaluateCullBounds, objects={}, players={}, triggers={}, effects={}, grouped={}, groupedNearProtected={}, groupedFarProtected={}, rotating={}, parentlessForcedVisible={}, invalidBoundsForcedVisible={}. Flags: forceVisible=bit0, trigger=bit1, effect=bit2, grouped=bit3, groupedNear=bit4, parentless=bit5, invalidBounds=bit6, player=bit7, rotating=bit8.",
            result.size(),playerCount,triggerCount,effectCount,groupedCount,groupedNearCount,groupedFarCount,rotatingCount,parentlessCount,invalidBoundsCount);
        return result;
    }

    void reportSceneGraph(char const* stage) {
        log::info(
            "DashCore 0.10.12-alpha SCENE-GRAPH {}: scans={}, nodes={}, gameObjects={}, effects={}, grouped={}, hiddenGameObjects={}, maxChildren={}, maxDepth={}, lastScan={}us, engineInitFlag={}.",
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
            "DashCore 0.10.12-alpha {} buckets: <=2ms={}, 2-4ms={}, 4-8ms={}, 8-16ms={}, 16-25ms={}, >25ms={}.",
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
            "DashCore 0.10.12-alpha {} callback intervals: <=16.67ms={}, 16.67-25ms={}, 25-50ms={}, 50-100ms={}, >100ms={}, >50ms={}, >100ms={}, p50={}us, p95={}us, p99={}us.",
            label, b[0], b[1], b[2], b[3], b[4], over50ms, over100ms,
            gapPercentile(samples, 50), gapPercentile(samples, 95), gapPercentile(samples, 99)
        );
    }

    void runFinalSceneDiagnostics() {
        if (g_gpuObjectProbeStarted) return;
        g_gpuObjectProbeStarted = true;
        auto* root = static_cast<CCNode*>(g_profile.layer);
        if (!root) {
            log::error("DashCore 0.10.12-alpha DIAGNOSTIC-COST: skipped because the active PlayLayer root is null.");
            return;
        }

        // Geometry Dash's Android renderer did not invoke the hooked
        // CCDirector::drawScene path in AutoTest. Run this read-only scan from
        // the confirmed PlayLayer::postUpdate hook after 600 updates instead.
        auto const scanStart = Clock::now();
        auto const finalScan = inspectSceneGraph(root);
        auto const arrayScanStart = Clock::now();
        auto const objectArray = inspectPlayLayerObjectArray(g_profile.layer);
        auto const objectArrayScanUs = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
            Clock::now() - arrayScanStart).count();
        auto const scanUs = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
            Clock::now() - scanStart).count();
        g_profile.sceneScans = 1;
        g_profile.sceneNodes = finalScan.nodes;
        g_profile.sceneGameObjects = finalScan.gameObjects;
        g_profile.sceneEffectObjects = finalScan.effectObjects;
        g_profile.sceneGroupedObjects = finalScan.groupedObjects;
        g_profile.sceneHiddenGameObjects = finalScan.hiddenGameObjects;
        g_profile.sceneMaxChildren = finalScan.maxChildren;
        g_profile.sceneMaxDepth = finalScan.maxDepth;
        g_profile.sceneScanUs = scanUs;
        reportSceneGraph("FINAL-REFRESHED");

        auto const collectStart = Clock::now();
        auto const inputs = collectGpuCullInputs(g_profile.layer);
        auto const collectUs = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
            Clock::now() - collectStart).count();

        auto* director = cocos2d::CCDirector::sharedDirector();
        auto const screen = director->getWinSize();
        auto const visibleSize = director->getVisibleSize();
        auto const visibleOrigin = director->getVisibleOrigin();
        auto const cameraPosition = g_profile.layer->m_gameState.m_cameraPosition;
        auto const cameraZoom = g_profile.layer->m_gameState.m_cameraZoom;
        log::info(
            "DashCore 0.10.12-alpha VIEWPORT-SPACE: winSize=({:.2f},{:.2f}), visibleOrigin=({:.2f},{:.2f}), visibleSize=({:.2f},{:.2f}), contentScaleFactor={:.3f}, cameraPosition=({:.2f},{:.2f}), cameraZoom={:.4f}. Camera fields are diagnostic metadata; no additional camera transform is applied in this bounds pass.",
            screen.width,screen.height,visibleOrigin.x,visibleOrigin.y,visibleSize.width,visibleSize.height,
            director->getContentScaleFactor(),cameraPosition.x,cameraPosition.y,cameraZoom
        );
        auto const boundsStart = Clock::now();
        auto const bounds = validateObjectBounds(root, screen.width, screen.height);
        auto const boundsUs = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
            Clock::now() - boundsStart).count();

        auto const arrayBoundsStart = Clock::now();
        auto const arrayBounds = validateObjectArrayBounds(
            g_profile.layer,visibleSize.width,visibleSize.height,visibleOrigin.x,visibleOrigin.y
        );
        auto const arrayBoundsUs = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
            Clock::now() - arrayBoundsStart).count();

        auto const geometryStart = Clock::now();
        auto const geometry = inspectObjectGeometry(root);
        auto const geometryUs = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
            Clock::now() - geometryStart).count();

        log::info(
            "DashCore 0.10.12-alpha DIAGNOSTIC-COST: sceneScan={}us, objectArrayScan={}us, gpuInputCollection={}us, boundsValidation={}us, arrayBoundsValidation={}us, objectGeometry={}us, gpuInputs={}, treeBoundsObjects={}, arrayBoundsObjects={}, arrayEntries={}.",
            scanUs,objectArrayScanUs,collectUs,boundsUs,arrayBoundsUs,geometryUs,
            inputs.size(),bounds.scannedGameObjects,arrayBounds.scannedGameObjects,objectArray.entries
        );
        log::info(
            "DashCore 0.10.12-alpha BISMUTH-CULL: treeAndArrayCoverage; treeBoundsSource=PlayLayer-child-tree, gpuBoundsSource=PlayLayer::m_objects+evaluateCullBounds, groupedPolicy=force-visible, playerPolicy=force-visible; rootChildren={}, treeNodes={}, treeGameObjects={}, arrayEntries={}, arrayGameObjects={}, gpuInputs={}, treeBoundsObjects={}, arrayBoundsObjects={}, geometryTreeObjects={}.",
            root->getChildrenCount(),finalScan.nodes,finalScan.gameObjects,objectArray.entries,objectArray.objects,
            inputs.size(),bounds.scannedGameObjects,arrayBounds.scannedGameObjects,geometry.objects
        );
        reportBoundsCandidate("parent-transformed", bounds.parentTransformed);
        reportBoundsCandidate("raw-rect", bounds.rawRect);
        reportBoundsCandidate("node-local", bounds.nodeLocal);
        reportBoundsCandidate("array-parent-transformed",arrayBounds.parentTransformed);
        reportBoundsCandidate("array-raw-rect",arrayBounds.rawRect);
        reportBoundsCandidate("array-node-local",arrayBounds.nodeLocal);
        reportObjectGeometry(geometry);
        dashcore::bismuth::runGLESObjectCullingProbeOnce(inputs,visibleSize.width,visibleSize.height);
    }

    void maybeFinishProfile() {
        if (g_profile.updateFrames >= kFramesToMeasure && g_gpuObjectProbeStarted) {
            log::info("DashCore 0.10.12-alpha: 600 gameplay updates and the isolated scene-geometry scan are complete; diagnostic-only.");
            g_profile.layer = nullptr;
        }
    }

    void reportUpdateProfile(char const* stage) {
        if (g_profile.updateFrames == 0) return;
        log::info(
            "DashCore 0.10.12-alpha UPDATE {}: frames={}, avg={}us, max={}us, over8ms={}, callbackGap>50ms={}, callbackGap>100ms={}, maxGap={}us, dtRange={:.4f}-{:.4f}s.",
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
        log::info("DashCore 0.10.12-alpha NODE-VISIT-COST {}: sampledNodeVisits={}, inclusiveTotal={}us, inclusiveMax={}us, sampledGameObjectVisits={}, gameObjectInclusiveTotal={}us, gameObjectInclusiveMax={}us. Inclusive totals overlap nested parent/child visits and must not be added to DrawScene CPU time.",
            stage, g_profile.sampledVisitCalls, g_profile.sampledVisitTotalUs,
            g_profile.sampledVisitMaxUs, g_profile.sampledGameObjectVisits,
            g_profile.sampledGameObjectVisitTotalUs, g_profile.sampledGameObjectVisitMaxUs);
    }

    void reportCullStats(char const* stage) {
        log::info(
            "DashCore 0.10.12-alpha CULL {}: requested={}, active={}, frames={}, visited={}, eligible={}, protectedPlayer={}, protectedEffect={}, protectedTrigger={}, protectedGroupedNear={}, eligibleGroupedFar={}, culledGroupedFar={}, boundsChecks={}, culled={}.",
            stage, g_cullingRequested, g_cullingEnabled, g_profile.drawFrames,
            g_profile.visitedObjects, g_profile.eligibleObjects,
            g_profile.protectedPlayerObjects, g_profile.protectedEffectObjects, g_profile.protectedTriggerObjects,
            g_profile.protectedGroupedNearObjects, g_profile.eligibleGroupedFarObjects,
            g_profile.culledGroupedFarObjects, g_profile.boundsChecks, g_profile.culledObjects
        );
    }

    void reportDrawProfile(char const* stage) {
        if (g_profile.drawFrames == 0) return;
        log::info(
            "DashCore 0.10.12-alpha DRAWSCENE-CPU {}: frames={}, avg={}us, max={}us, over8ms={}, callbackGap>50ms={}, callbackGap>100ms={}, maxGap={}us. GPU execution time is not measured.",
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
    void reportRenderCallback(char const* name, RenderCallbackMetric const& metric) {
        auto const averageUs = metric.timedCalls
            ? static_cast<double>(metric.inclusiveTotalUs) / static_cast<double>(metric.timedCalls)
            : 0.0;
        log::info(
            "DashCore 0.10.12-alpha RENDER-CALLBACK {}: calls={}, timedCalls={}, inclusiveAvgUs={:.2f}, inclusiveTotalUs={}, inclusiveMaxUs={}.",
            name, metric.calls, metric.timedCalls, averageUs,
            metric.inclusiveTotalUs, metric.inclusiveMaxUs
        );
    }


    struct ObjectVisualSample {
        int objectID = -1;
        int classType = -1;
        std::uint32_t directChildren = 0;
        std::uint32_t descendantNodes = 0;
        std::uint32_t descendantSprites = 0;
        std::uint32_t descendantBatchNodes = 0;
        std::uint32_t nestedGameObjects = 0;
        std::uint32_t maxDepth = 0;
        bool batchAncestor = false;
    };

    struct ObjectVisualHierarchyStats {
        std::uint64_t objectsScanned = 0;
        std::uint64_t nullEntries = 0;
        std::uint64_t objectsWithParent = 0;
        std::uint64_t objectsUnderBatchNode = 0;
        std::uint64_t objectsWithChildren = 0;
        std::uint64_t leafObjects = 0;
        std::uint64_t objectsWithDescendantSprites = 0;
        std::uint64_t directChildren = 0;
        std::uint64_t descendantNodes = 0;
        std::uint64_t descendantSprites = 0;
        std::uint64_t descendantBatchNodes = 0;
        std::uint64_t nestedGameObjects = 0;
        std::uint64_t parentDepthTotal = 0;
        std::uint64_t childArrayCountMismatches = 0;
        std::uint64_t truncatedObjects = 0;
        std::uint64_t depthTruncatedObjects = 0;
        std::uint64_t depthLimitHits = 0;
        std::uint32_t parentDepthMax = 0;
        std::uint32_t maxDirectChildren = 0;
        std::uint32_t maxDescendantNodesPerObject = 0;
        std::uint32_t maxDescendantSpritesPerObject = 0;
        std::uint32_t maxDepth = 0;
        std::array<std::uint64_t, 5> descendantBuckets{};
        bool scanTruncated = false;
    };

    constexpr std::uint32_t kMaxObjectVisualNodesPerObject = 4096;
    constexpr std::uint32_t kMaxObjectVisualDepth = 64;
    constexpr std::uint64_t kMaxTotalObjectVisualNodes = 250000;
    constexpr std::size_t kObjectVisualTopSampleCount = 8;

    // One-shot read-only inventory of each root in PlayLayer::m_objects and
    // its Cocos child subtree. A GameObject can own multiple sprites/nodes.
    // Descendant totals are per root and can overlap if GameObjects are nested.
    void reportObjectVisualHierarchy(PlayLayer* layer) {
        auto const scanStart = Clock::now();
        ObjectVisualHierarchyStats stats;
        std::array<ObjectVisualSample, kObjectVisualTopSampleCount> topSamples{};
        std::size_t topSampleCount = 0;

        if (!layer || !layer->m_objects) {
            log::warn(
                "DashCore 0.10.12-alpha OBJECT-VISUAL-HIERARCHY: PlayLayer::m_objects unavailable; scan skipped."
            );
            return;
        }

        auto const arrayEntries = static_cast<std::uint64_t>(layer->m_objects->count());
        std::vector<std::pair<CCNode*, std::uint32_t>> pending;
        pending.reserve(32);

        for (auto object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            if (stats.descendantNodes >= kMaxTotalObjectVisualNodes) {
                stats.scanTruncated = true;
                break;
            }
            if (!object) {
                ++stats.nullEntries;
                continue;
            }
            ++stats.objectsScanned;

            auto const directChildren = static_cast<std::uint32_t>(object->getChildrenCount());
            auto* childArray = object->getChildren();
            stats.directChildren += directChildren;
            stats.maxDirectChildren = std::max(stats.maxDirectChildren, directChildren);
            if (directChildren == 0) {
                ++stats.leafObjects;
            } else if (childArray) {
                ++stats.objectsWithChildren;
            } else {
                ++stats.childArrayCountMismatches;
            }
            if (childArray &&
                static_cast<std::uint64_t>(childArray->count()) != directChildren) {
                ++stats.childArrayCountMismatches;
            }

            bool batchAncestor = false;
            std::uint32_t parentDepth = 0;
            for (auto* ancestor = object->getParent(); ancestor; ancestor = ancestor->getParent()) {
                ++parentDepth;
                if (typeinfo_cast<cocos2d::CCSpriteBatchNode*>(ancestor)) {
                    batchAncestor = true;
                }
            }
            if (object->getParent()) {
                ++stats.objectsWithParent;
                stats.parentDepthTotal += parentDepth;
                stats.parentDepthMax = std::max(stats.parentDepthMax, parentDepth);
            }
            if (batchAncestor) ++stats.objectsUnderBatchNode;

            std::uint32_t objectNodes = 0;
            std::uint32_t objectSprites = 0;
            std::uint32_t objectBatchNodes = 0;
            std::uint32_t objectGameObjects = 0;
            std::uint32_t objectMaxDepth = 0;
            bool objectDepthTruncated = false;
            pending.clear();

            if (childArray) {
                for (auto* child : CCArrayExt<CCNode*>(childArray)) {
                    if (child) pending.emplace_back(child, 1);
                }
            }

            while (!pending.empty() &&
                   objectNodes < kMaxObjectVisualNodesPerObject &&
                   stats.descendantNodes < kMaxTotalObjectVisualNodes) {
                auto const entry = pending.back();
                pending.pop_back();
                auto* node = entry.first;
                auto const depth = entry.second;
                if (!node) continue;

                ++objectNodes;
                ++stats.descendantNodes;
                objectMaxDepth = std::max(objectMaxDepth, depth);

                if (typeinfo_cast<cocos2d::CCSprite*>(node)) {
                    ++objectSprites;
                    ++stats.descendantSprites;
                }
                if (typeinfo_cast<cocos2d::CCSpriteBatchNode*>(node)) {
                    ++objectBatchNodes;
                    ++stats.descendantBatchNodes;
                }
                if (typeinfo_cast<GameObject*>(node)) {
                    ++objectGameObjects;
                    ++stats.nestedGameObjects;
                }

                auto const nestedChildCount = node->getChildrenCount();
                auto* nestedChildren = node->getChildren();
                if (nestedChildCount == 0 || !nestedChildren) continue;
                if (depth >= kMaxObjectVisualDepth) {
                    objectDepthTruncated = true;
                    continue;
                }
                for (auto* child : CCArrayExt<CCNode*>(nestedChildren)) {
                    if (child) pending.emplace_back(child, depth + 1);
                }
            }

            if (!pending.empty()) ++stats.truncatedObjects;
            if (objectDepthTruncated) {
                ++stats.depthTruncatedObjects;
                ++stats.depthLimitHits;
            }
            if (objectSprites > 0) ++stats.objectsWithDescendantSprites;

            stats.maxDescendantNodesPerObject =
                std::max(stats.maxDescendantNodesPerObject, objectNodes);
            stats.maxDescendantSpritesPerObject =
                std::max(stats.maxDescendantSpritesPerObject, objectSprites);
            stats.maxDepth = std::max(stats.maxDepth, objectMaxDepth);

            if (objectNodes == 0) ++stats.descendantBuckets[0];
            else if (objectNodes <= 2) ++stats.descendantBuckets[1];
            else if (objectNodes <= 5) ++stats.descendantBuckets[2];
            else if (objectNodes <= 10) ++stats.descendantBuckets[3];
            else ++stats.descendantBuckets[4];

            ObjectVisualSample sample;
            sample.objectID = static_cast<int>(object->m_objectID);
            sample.classType = static_cast<int>(object->m_classType);
            sample.directChildren = directChildren;
            sample.descendantNodes = objectNodes;
            sample.descendantSprites = objectSprites;
            sample.descendantBatchNodes = objectBatchNodes;
            sample.nestedGameObjects = objectGameObjects;
            sample.maxDepth = objectMaxDepth;
            sample.batchAncestor = batchAncestor;

            if (topSampleCount < topSamples.size()) {
                topSamples[topSampleCount++] = sample;
            } else {
                auto worst = std::min_element(
                    topSamples.begin(), topSamples.end(),
                    [](ObjectVisualSample const& left, ObjectVisualSample const& right) {
                        return left.descendantNodes < right.descendantNodes;
                    }
                );
                if (worst != topSamples.end() &&
                    sample.descendantNodes > worst->descendantNodes) {
                    *worst = sample;
                }
            }
        }

        auto const scanUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                Clock::now() - scanStart).count());
        auto const meanDirectChildren = stats.objectsScanned
            ? static_cast<double>(stats.directChildren) / static_cast<double>(stats.objectsScanned)
            : 0.0;
        auto const meanDescendantNodes = stats.objectsScanned
            ? static_cast<double>(stats.descendantNodes) / static_cast<double>(stats.objectsScanned)
            : 0.0;
        auto const meanDescendantSprites = stats.objectsScanned
            ? static_cast<double>(stats.descendantSprites) / static_cast<double>(stats.objectsScanned)
            : 0.0;
        auto const meanParentDepth = stats.objectsWithParent
            ? static_cast<double>(stats.parentDepthTotal) / static_cast<double>(stats.objectsWithParent)
            : 0.0;

        log::info(
            "DashCore 0.10.12-alpha OBJECT-VISUAL-HIERARCHY: source=PlayLayer::m_objects, arrayEntries={}, objectsScanned={}, nullEntries={}, objectsWithParent={}, objectsUnderBatchNode={}, objectsWithChildren={}, leafObjects={}, directChildren={}, descendantNodes={}, descendantSprites={}, descendantBatchNodes={}, nestedGameObjects={}, objectsWithDescendantSprites={}, meanDirectChildren={:.2f}, meanDescendantNodes={:.2f}, meanDescendantSprites={:.2f}, meanParentDepth={:.2f}, parentDepthMax={}, maxDirectChildren={}, maxDescendantNodesPerObject={}, maxDescendantSpritesPerObject={}, maxDepth={}, childArrayCountMismatches={}, truncatedObjects={}, depthTruncatedObjects={}, scanTruncated={}, scanUs={}.",
            arrayEntries, stats.objectsScanned, stats.nullEntries,
            stats.objectsWithParent, stats.objectsUnderBatchNode, stats.objectsWithChildren,
            stats.leafObjects, stats.directChildren, stats.descendantNodes,
            stats.descendantSprites, stats.descendantBatchNodes, stats.nestedGameObjects,
            stats.objectsWithDescendantSprites, meanDirectChildren, meanDescendantNodes,
            meanDescendantSprites, meanParentDepth, stats.parentDepthMax,
            stats.maxDirectChildren, stats.maxDescendantNodesPerObject,
            stats.maxDescendantSpritesPerObject, stats.maxDepth,
            stats.childArrayCountMismatches, stats.truncatedObjects,
            stats.depthTruncatedObjects, stats.scanTruncated, scanUs
        );
        log::info(
            "DashCore 0.10.12-alpha OBJECT-VISUAL-BUCKETS: zero={}, oneToTwo={}, threeToFive={}, sixToTen={}, elevenPlus={}.",
            stats.descendantBuckets[0], stats.descendantBuckets[1],
            stats.descendantBuckets[2], stats.descendantBuckets[3],
            stats.descendantBuckets[4]
        );

        std::sort(
            topSamples.begin(), topSamples.begin() + topSampleCount,
            [](ObjectVisualSample const& left, ObjectVisualSample const& right) {
                return left.descendantNodes > right.descendantNodes;
            }
        );
        for (std::size_t i = 0; i < topSampleCount; ++i) {
            auto const& sample = topSamples[i];
            log::info(
                "DashCore 0.10.12-alpha OBJECT-VISUAL-SAMPLE: rank={}, objectID={}, classType={}, directChildren={}, descendantNodes={}, descendantSprites={}, descendantBatchNodes={}, nestedGameObjects={}, maxDepth={}, batchAncestor={}.",
                i + 1, sample.objectID, sample.classType, sample.directChildren,
                sample.descendantNodes, sample.descendantSprites,
                sample.descendantBatchNodes, sample.nestedGameObjects,
                sample.maxDepth, sample.batchAncestor
            );
        }
        log::info(
            "DashCore 0.10.12-alpha OBJECT-VISUAL-NOTE: child sprites/nodes are counted from PlayLayer::m_objects Cocos subtrees; the root GameObject is excluded. Per-root totals can overlap for nested GameObjects. A SpriteBatchNode ancestor is a hierarchy clue, not proof of atlas membership. The scan is read-only, one-shot, and reports when its safety caps truncate traversal."
        );
    }

    constexpr std::size_t kMaxObjectSectionOuterSlots = 1024;
    constexpr std::size_t kMaxObjectSectionCellSlots = 32768;

    struct ObjectSectionLayoutStats {
        std::uint64_t objectArrayEntries = 0;
        std::uint64_t activeVectorSlots = 0;
        int activeObjectsCount = 0;
        int activeObjectsIndex = 0;
        std::uint64_t sectionOuterSlots = 0;
        std::uint64_t outerSlotsScanned = 0;
        std::uint64_t sectionCellsScanned = 0;
        std::uint64_t nonNullSectionCells = 0;
        std::uint64_t emptySectionCells = 0;
        std::uint64_t nullOuterEntries = 0;
        std::uint64_t nullCellEntries = 0;
        std::uint64_t sectionObjectReferences = 0;
        std::uint64_t maxObjectsPerCell = 0;
        std::uint64_t outerSlotsWithCellSlots = 0;
        std::uint64_t fullyScannedOuterSlots = 0;
        std::uint64_t partiallyScannedOuterSlots = 0;
        std::uint64_t outerSlotsWithObjectReferences = 0;
        std::uint64_t maxCellsPerOuterSlot = 0;
        std::uint64_t maxObjectReferencesPerOuterSlot = 0;
        bool truncated = false;
    };

    // Read-only snapshot of Geometry Dash's existing active-object list and
    // section grid. It reads container sizes only; it never dereferences the
    // GameObject pointers stored in section cells.
    void reportObjectSectionLayout(PlayLayer* layer) {
        auto const scanStart = Clock::now();
        ObjectSectionLayoutStats stats;
        if (!layer) {
            log::warn("DashCore 0.10.12-alpha OBJECT-SECTION-LAYOUT: PlayLayer unavailable; snapshot skipped.");
            return;
        }

        stats.objectArrayEntries = layer->m_objects
            ? static_cast<std::uint64_t>(layer->m_objects->count())
            : 0;
        stats.activeVectorSlots = static_cast<std::uint64_t>(layer->m_activeObjects.size());
        stats.activeObjectsCount = layer->m_activeObjectsCount;
        stats.activeObjectsIndex = layer->m_activeObjectsIndex;
        stats.sectionOuterSlots = static_cast<std::uint64_t>(layer->m_sections.size());

        auto const outerLimit = std::min<std::size_t>(
            layer->m_sections.size(), kMaxObjectSectionOuterSlots
        );
        for (std::size_t outerIndex = 0; outerIndex < outerLimit; ++outerIndex) {
            ++stats.outerSlotsScanned;
            auto* row = layer->m_sections[outerIndex];
            if (!row) {
                ++stats.nullOuterEntries;
                continue;
            }

            auto const innerSlots = row->size();
            if (innerSlots > 0) ++stats.outerSlotsWithCellSlots;
            stats.maxCellsPerOuterSlot = std::max(
                stats.maxCellsPerOuterSlot, static_cast<std::uint64_t>(innerSlots)
            );

            std::uint64_t outerSlotObjectReferences = 0;
            std::size_t outerSlotCellsScanned = 0;
            for (std::size_t innerIndex = 0; innerIndex < innerSlots; ++innerIndex) {
                if (stats.sectionCellsScanned >= kMaxObjectSectionCellSlots) {
                    stats.truncated = true;
                    break;
                }
                ++stats.sectionCellsScanned;
                ++outerSlotCellsScanned;
                auto* cell = (*row)[innerIndex];
                if (!cell) {
                    ++stats.nullCellEntries;
                    continue;
                }

                auto const objectCount = static_cast<std::uint64_t>(cell->size());
                ++stats.nonNullSectionCells;
                if (objectCount == 0) ++stats.emptySectionCells;
                stats.sectionObjectReferences += objectCount;
                outerSlotObjectReferences += objectCount;
                stats.maxObjectsPerCell = std::max(stats.maxObjectsPerCell, objectCount);
            }

            // Per-outer-slot reference totals are considered complete only when
            // every cell in that slot was inspected; a capped partial row is
            // counted separately and excluded from the complete-row maximum.
            if (outerSlotCellsScanned == innerSlots) {
                ++stats.fullyScannedOuterSlots;
                if (outerSlotObjectReferences > 0) {
                    ++stats.outerSlotsWithObjectReferences;
                }
                stats.maxObjectReferencesPerOuterSlot = std::max(
                    stats.maxObjectReferencesPerOuterSlot, outerSlotObjectReferences
                );
            } else {
                ++stats.partiallyScannedOuterSlots;
            }

            if (stats.truncated) break;
        }
        if (stats.sectionOuterSlots > kMaxObjectSectionOuterSlots) {
            stats.truncated = true;
        }

        auto const scanUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                Clock::now() - scanStart).count());
        log::info(
            "DashCore 0.10.12-alpha OBJECT-SECTION-LAYOUT: objectArrayEntries={}, activeVectorSlots={}, activeObjectsCount={}, activeObjectsIndex={}, sectionOuterSlots={}, outerSlotsScanned={}, sectionCellsScanned={}, nonNullCells={}, emptyCells={}, nullOuterEntries={}, nullCellEntries={}, sectionObjectReferences={}, maxObjectsPerCell={}, truncated={}, scanUs={}.",
            stats.objectArrayEntries, stats.activeVectorSlots, stats.activeObjectsCount,
            stats.activeObjectsIndex, stats.sectionOuterSlots, stats.outerSlotsScanned,
            stats.sectionCellsScanned, stats.nonNullSectionCells, stats.emptySectionCells,
            stats.nullOuterEntries, stats.nullCellEntries, stats.sectionObjectReferences,
            stats.maxObjectsPerCell, stats.truncated, scanUs
        );
        log::info(
            "DashCore 0.10.12-alpha OBJECT-SECTION-ROWS: outerSlotsWithCellSlots={}, fullyScannedOuterSlots={}, partiallyScannedOuterSlots={}, outerSlotsWithObjectReferences={}, maxCellsPerOuterSlot={}, maxObjectReferencesPerOuterSlot={}.",
            stats.outerSlotsWithCellSlots, stats.fullyScannedOuterSlots,
            stats.partiallyScannedOuterSlots, stats.outerSlotsWithObjectReferences,
            stats.maxCellsPerOuterSlot, stats.maxObjectReferencesPerOuterSlot
        );
        log::info(
            "DashCore 0.10.12-alpha OBJECT-SECTION-NOTE: this is a bounded read-only snapshot of m_activeObjects and nested m_sections container sizes. Section dimensions are reported structurally, not assumed to represent X/Y. Stored GameObject pointers are not dereferenced; sectionObjectReferences is a raw slot total, not a proven unique-object count. Per-outer-slot reference aggregates and their maximum include fully scanned non-null rows only."
        );
    }

    void recordRenderPipelineUpdate(
        bool sampling, Clock::time_point start, float dt
    ) {
        if (!sampling) return;
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                Clock::now() - start).count());
        ++g_rendererProfile.updateFrames;
        g_rendererProfile.postUpdateTotalUs += elapsedUs;
        g_rendererProfile.postUpdateMaxUs = std::max(
            g_rendererProfile.postUpdateMaxUs, elapsedUs
        );
        recordTimingBucket(g_rendererProfile.postUpdateDurationBuckets, elapsedUs);
        if (g_rendererProfile.updateFrames % 30 == 0) {
            log::info(
                "DashCore 0.10.12-alpha RENDER-UPDATE-PROGRESS: postUpdateFrames={}, drawSceneFrames={}, sampleActive={}.",
                g_rendererProfile.updateFrames, g_rendererProfile.frames,
                g_rendererProfile.active
            );
        }
        if (elapsedUs > kSlowCallbackUs) ++g_rendererProfile.slowPostUpdates;
        g_rendererProfile.minUpdateDt = std::min(g_rendererProfile.minUpdateDt, dt);
        g_rendererProfile.maxUpdateDt = std::max(g_rendererProfile.maxUpdateDt, dt);
    }

    void reportRendererPipeline(char const* stage) {
        auto const averageDrawUs = g_rendererProfile.frames
            ? static_cast<double>(g_rendererProfile.drawSceneTotalUs) / static_cast<double>(g_rendererProfile.frames)
            : 0.0;
        auto const averagePostUpdateUs = g_rendererProfile.updateFrames
            ? static_cast<double>(g_rendererProfile.postUpdateTotalUs) / static_cast<double>(g_rendererProfile.updateFrames)
            : 0.0;
        auto const minUpdateDt = g_rendererProfile.updateFrames
            ? g_rendererProfile.minUpdateDt
            : 0.f;
        auto const sampleCoverage = g_rendererProfile.frames >= kRenderPipelineSampleFrames
            ? "draw-and-update"
            : (g_rendererProfile.frames > 0 ? "partial-draw" : "update-only");
        log::info(
            "DashCore 0.10.12-alpha RENDER-PIPELINE {}: frames={}, drawSceneFrames={}, postUpdateFrames={}, sampleCoverage={}, timedDrawFrames={}, timingStride={}, drawSceneAvgUs={:.2f}, drawSceneTotalUs={}, drawSceneMaxUs={}.",
            stage, g_rendererProfile.updateFrames, g_rendererProfile.frames,
            g_rendererProfile.updateFrames, sampleCoverage,
            g_rendererProfile.timedDrawFrames, kRenderPipelineTimingStride,
            averageDrawUs, g_rendererProfile.drawSceneTotalUs, g_rendererProfile.drawSceneMaxUs
        );
        log::info(
            "DashCore 0.10.12-alpha RENDER-UPDATE: frames={}, avgPostUpdateUs={:.2f}, maxPostUpdateUs={}, slowPostUpdates={}, dtMin={:.4f}, dtMax={:.4f}.",
            g_rendererProfile.updateFrames, averagePostUpdateUs,
            g_rendererProfile.postUpdateMaxUs, g_rendererProfile.slowPostUpdates,
            minUpdateDt, g_rendererProfile.maxUpdateDt
        );
        reportRenderCallback("CCNode::visit", g_rendererProfile.nodeVisit);
        reportRenderCallback("CCSprite::draw", g_rendererProfile.spriteDraw);
        reportRenderCallback("CCSpriteBatchNode::draw", g_rendererProfile.batchDraw);
        reportRenderCallback("CCTextureAtlas::drawQuads", g_rendererProfile.atlasDrawQuads);
        reportRenderCallback("CCTextureAtlas::drawNumberOfQuads", g_rendererProfile.atlasDrawNumberOfQuads);
        reportRenderCallback("GJBaseGameLayer::update (PlayLayer)", g_rendererProfile.baseGameLayerUpdate);
        reportRenderCallback("PlayLayer::updateVisibility", g_rendererProfile.playLayerUpdateVisibility);
        reportTimingBuckets("CCDirector::drawScene", g_rendererProfile.drawSceneDurationBuckets);
        reportTimingBuckets("PlayLayer::postUpdate", g_rendererProfile.postUpdateDurationBuckets);
        reportTimingBuckets("CCNode::visit", g_rendererProfile.nodeVisit.inclusiveDurationBuckets);
        reportTimingBuckets("CCSprite::draw", g_rendererProfile.spriteDraw.inclusiveDurationBuckets);
        reportTimingBuckets("CCSpriteBatchNode::draw", g_rendererProfile.batchDraw.inclusiveDurationBuckets);
        reportTimingBuckets("CCTextureAtlas::drawQuads", g_rendererProfile.atlasDrawQuads.inclusiveDurationBuckets);
        reportTimingBuckets("CCTextureAtlas::drawNumberOfQuads", g_rendererProfile.atlasDrawNumberOfQuads.inclusiveDurationBuckets);
        reportTimingBuckets("GJBaseGameLayer::update (PlayLayer)", g_rendererProfile.baseGameLayerUpdate.inclusiveDurationBuckets);
        reportTimingBuckets("PlayLayer::updateVisibility", g_rendererProfile.playLayerUpdateVisibility.inclusiveDurationBuckets);
        log::info(
            "DashCore 0.10.12-alpha RENDER-UPDATE-BINS-NOTE: PlayLayer::postUpdate covers all 180 sample updates; drawScene coverage is recorded separately and may be unavailable on Android. Renderer/update subphase buckets include timed callbacks only (one in every {} calls). Nested callback times must not be added and are not GPU timings.",
            kRenderPipelineTimingStride
        );
        log::info(
            "DashCore 0.10.12-alpha RENDER-UPDATE-SUBPHASES: baseGameLayerUpdateHookReady={}, playLayerVisibilityHookReady={}. The two callbacks are measured independently when available; visibility timing may be nested inside base-layer update timing.",
            g_baseGameLayerUpdateHookReady, g_playLayerVisibilityHookReady
        );
        log::info(
            "DashCore 0.10.12-alpha RENDER-PIPELINE-NOTE: PlayLayer::postUpdate is the authoritative 180-update sample clock. CCDirector::drawScene is supplementary; zero drawSceneFrames means its callback was not observed and no draw-path coverage is claimed. Cocos callback times are inclusive and nested; do not sum them. Subphase timing samples every {}th callback and is CPU-only, not GPU execution time.",
            kRenderPipelineTimingStride
        );
    }

    void finishRendererPipelineSampleIfReady() {
        if (!g_rendererProfile.active || g_rendererProfile.finalReported ||
            g_rendererProfile.updateFrames < kRenderPipelineSampleFrames) {
            return;
        }

        g_rendererProfile.finalReported = true;
        g_rendererProfile.active = false;
        reportRendererPipeline("FINAL");
        reportObjectVisualHierarchy(g_profile.layer);
        reportObjectSectionLayout(g_profile.layer);
    }

    void startRendererPipelineSample(PlayLayer* layer) {
        g_rendererProfile = {};
        if (!g_renderPipelineHooksReady || !layer) return;
        g_profile.layer = layer;
        g_rendererProfile.active = true;
        log::info(
            "DashCore 0.10.12-alpha RENDER-PIPELINE: started read-only {}-frame sample; timing every {} frames.",
            kRenderPipelineSampleFrames, kRenderPipelineTimingStride
        );
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
        g_nodeVisitHook = registerOptionalHook(self, "::visit", "CCNode::visit");
    }

    void visit() {
        [[maybe_unused]] RenderCallScope renderCall(
            g_rendererProfile.nodeVisit,
            g_generalEnabled && g_rendererProfile.active,
            g_rendererProfile.sampleThisFrame
        );
        if (!g_generalEnabled || (!g_diagnosticsEnabled && !g_cullingEnabled && !g_rendererProfile.active)) {
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

        bool const profiling = g_diagnosticsEnabled && g_profile.layer != nullptr &&
            g_profile.updateFrames < kFramesToMeasure;
        bool const sampleVisit = profiling &&
            (g_profile.updateFrames % kReportEvery == 0);
        if (g_diagnosticsEnabled && g_profile.layer != nullptr &&
            g_profile.updateFrames >= kFramesToMeasure &&
            !g_profile.nodeVisitReported) {
            reportVisitCost("FINAL");
            g_profile.nodeVisitReported = true;
        }
        auto visitBase = [&]() {
            if (!sampleVisit) {
                CCNode::visit();
                return;
            }
            auto const visitStart = Clock::now();
            CCNode::visit();
            auto const visitUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
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

        // With culling disabled, perform RTTI and classification only on
        // diagnostic sample frames. When experimental culling is enabled,
        // classify each object visit but time only diagnostic samples.
        if (!g_cullingEnabled && !sampleVisit) {
            CCNode::visit();
            return;
        }

        auto* object = typeinfo_cast<GameObject*>(this);
        if (!object) {
            visitBase();
            return;
        }

        if (g_cullingEnabled && g_cullingTelemetryEnabled && g_cullingSamplingActive) {
            ++g_cullingVisitedObjects;
        }
        if (sampleVisit) ++g_profile.visitedObjects;

        bool const isPlayer = typeinfo_cast<PlayerObject*>(object) != nullptr;
        bool const isEffect = object->m_classType == GameObjectClassType::Effect;
        bool const isTrigger = object->isTrigger();
        bool const isGrouped = object->m_groupCount > 0;
        bool const hasRotateAction = object->getHasRotateAction();
        bool const eligible = !isPlayer && !isEffect && !isTrigger && !isGrouped && !hasRotateAction;
        if (g_cullingEnabled && g_cullingTelemetryEnabled && g_cullingSamplingActive) {
            if (eligible) ++g_cullingEligibleObjects;
            else if (isPlayer) recordProtectedCullingReason(ProtectedCullingReason::Player);
            else if (isEffect) recordProtectedCullingReason(ProtectedCullingReason::Effect);
            else if (isTrigger) recordProtectedCullingReason(ProtectedCullingReason::Trigger);
            else if (isGrouped) recordProtectedCullingReason(ProtectedCullingReason::Grouped);
            else if (hasRotateAction) recordProtectedCullingReason(ProtectedCullingReason::Rotating);
        }

        // Existing diagnostics use the same sampled visit set for their profile totals.
        if (sampleVisit) {
            if (isPlayer) {
                ++g_profile.protectedPlayerObjects;
            } else if (isEffect) {
                ++g_profile.protectedEffectObjects;
            } else if (isTrigger) {
                ++g_profile.protectedTriggerObjects;
            } else if (isGrouped || hasRotateAction) {
                ++g_profile.protectedGroupedNearObjects;
            } else {
                ++g_profile.eligibleObjects;
                if (isGrouped) ++g_profile.eligibleGroupedFarObjects;
                if (g_cullingEnabled) ++g_profile.boundsChecks;
            }
        }

        if (!g_cullingEnabled || !eligible) {
            visitBase();
            return;
        }

        auto const evaluation = evaluateCullBounds(object);
        recordCullingBoundsEvaluation(object, evaluation);
        if (evaluation.shouldCull) {
            if (g_cullingTelemetryEnabled && g_cullingSamplingActive) ++g_cullingCulledObjects;
            if (sampleVisit) {
                ++g_profile.culledObjects;
            }
            return;
        }

        visitBase();
    }
};

class $modify(OptGodCCSprite, cocos2d::CCSprite) {
    static void onModify(auto& self) {
        g_spriteDrawHook = registerOptionalHook(self, "::draw", "CCSprite::draw");
    }

    void draw() {
        [[maybe_unused]] RenderCallScope renderCall(
            g_rendererProfile.spriteDraw,
            g_generalEnabled && g_rendererProfile.active,
            g_rendererProfile.sampleThisFrame
        );
        CCSprite::draw();
    }
};

class $modify(OptGodCCSpriteBatchNode, cocos2d::CCSpriteBatchNode) {
    static void onModify(auto& self) {
        g_batchDrawHook = registerOptionalHook(self, "::draw", "CCSpriteBatchNode::draw");
    }

    void draw() {
        [[maybe_unused]] RenderCallScope renderCall(
            g_rendererProfile.batchDraw,
            g_generalEnabled && g_rendererProfile.active,
            g_rendererProfile.sampleThisFrame
        );
        CCSpriteBatchNode::draw();
    }
};

class $modify(OptGodCCTextureAtlas, cocos2d::CCTextureAtlas) {
    static void onModify(auto& self) {
        g_atlasDrawQuadsHook = registerOptionalHook(self, "::drawQuads", "CCTextureAtlas::drawQuads");
        g_atlasDrawNumberOfQuadsHook = registerOptionalHook(
            self, "::drawNumberOfQuads", "CCTextureAtlas::drawNumberOfQuads"
        );
    }

    void drawQuads() {
        [[maybe_unused]] RenderCallScope renderCall(
            g_rendererProfile.atlasDrawQuads,
            g_generalEnabled && g_rendererProfile.active,
            g_rendererProfile.sampleThisFrame
        );
        CCTextureAtlas::drawQuads();
    }

    void drawNumberOfQuads(unsigned int numberOfQuads) {
        [[maybe_unused]] RenderCallScope renderCall(
            g_rendererProfile.atlasDrawNumberOfQuads,
            g_generalEnabled && g_rendererProfile.active,
            g_rendererProfile.sampleThisFrame
        );
        CCTextureAtlas::drawNumberOfQuads(numberOfQuads);
    }
};

// Optional virtual callback timing for the PlayLayer's inherited game-layer update.
// The hook is enabled only for the opt-in render-pipeline profile and only counts
// calls whose receiver is the active PlayLayer. It forwards the original once.
class $modify(OptGodGJBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        g_baseGameLayerUpdateHook = registerOptionalHook(
            self, "::update", "GJBaseGameLayer::update"
        );
    }

    void update(float dt) {
        auto* playLayer = typeinfo_cast<PlayLayer*>(this);
        bool const active = g_generalEnabled && g_rendererProfile.active &&
            playLayer && playLayer == g_profile.layer;
        bool const timed = active &&
            (g_rendererProfile.baseGameLayerUpdate.calls % kRenderPipelineTimingStride == 0);
        [[maybe_unused]] RenderCallScope metric(
            g_rendererProfile.baseGameLayerUpdate, active, timed
        );
        GJBaseGameLayer::update(dt);
    }
};

class $modify(OptGodPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        g_postUpdateHook = registerOptionalHook(self, "::postUpdate", "PlayLayer::postUpdate");
        g_playLayerVisibilityHook = registerOptionalHook(
            self, "::updateVisibility", "PlayLayer::updateVisibility"
        );
    }

    void updateVisibility(float dt) {
        bool const active = g_generalEnabled && g_rendererProfile.active &&
            g_profile.layer == this;
        bool const timed = active &&
            (g_rendererProfile.playLayerUpdateVisibility.calls % kRenderPipelineTimingStride == 0);
        [[maybe_unused]] RenderCallScope metric(
            g_rendererProfile.playLayerUpdateVisibility, active, timed
        );
        PlayLayer::updateVisibility(dt);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!g_generalEnabled) return PlayLayer::init(level, useReplay, dontCreateObjects);
        g_profile = {};
        g_rendererProfile = {};
        g_gpuObjectProbeStarted = false;
        g_cullingEnabled = false;
        g_cullingWarmupFrames = 0;
        g_cullingFrames = 0;
        g_cullingVisitedObjects = 0;
        g_cullingEligibleObjects = 0;
        g_cullingCulledObjects = 0;
        g_cullingReasons = {};
        g_cullingSamplingActive = false;
        g_lastEngineInitFlag = dontCreateObjects;
        if (!g_diagnosticsEnabled) {
            bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
            if (result) rememberLastPlayedLevel(level);
            return result;
        }

        auto const start = Clock::now();
        bool const result = PlayLayer::init(level, useReplay, dontCreateObjects);
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                Clock::now() - start).count());

        if (result) {
            rememberLastPlayedLevel(level);
            log::info(
                "DashCore 0.10.12-alpha: PlayLayer::init={}us ({}ms), levelID={}, replay={}, engineInitFlag={}. ",
                elapsedUs, elapsedUs / 1000, level ? level->m_levelID : 0,
                useReplay, dontCreateObjects
            );
        } else {
            log::warn("DashCore 0.10.12-alpha: PlayLayer::init failed after {}us.", elapsedUs);
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
            g_cullingEnabled = g_cullingRequested;
            if (g_renderPipelineHooksReady) {
                startRendererPipelineSample(this);
            }
            log::info("DashCore 0.10.12-alpha: setupHasCompleted; performance-diagnostics=OFF, render-pipeline-diagnostics-requested={}, render-pipeline-hooks-ready={}, static-culling-requested={}, static-culling-active={}.",
                g_renderPipelineDiagnosticsRequested, g_renderPipelineHooksReady,
                g_cullingRequested, g_cullingEnabled);
            return;
        }
        auto const start = Clock::now();
        PlayLayer::setupHasCompleted();
        auto const elapsedUs = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                Clock::now() - start).count());

        g_profile = {};
        g_profile.layer = this;
        startRendererPipelineSample(this);
        // g_cullingRequested is resolved once during mod load, including the AutoTest override.
        // Culling is opt-in and limited to ordinary, ungrouped, non-animated objects.
        // All uncertain object classes continue through the original renderer.
        g_cullingEnabled = g_cullingRequested;
        g_profile.engineInitFlag = g_lastEngineInitFlag;
        log::info(
            "DashCore 0.10.12-alpha: setupHasCompleted={}us ({}ms); performance-diagnostics=ON, render-pipeline-diagnostics-requested={}, render-pipeline-hooks-ready={}, culling-requested={}, culling-active={}, engineInitFlag={} (raw engine argument; not a gameplay-mode detector). Experimental static-object culling is opt-in; protected objects always use vanilla rendering.",
            elapsedUs, elapsedUs / 1000, g_renderPipelineDiagnosticsRequested,
            g_renderPipelineHooksReady, g_cullingRequested, g_cullingEnabled,
            g_profile.engineInitFlag
        );
    }

    void postUpdate(float dt) {
        bool const rendererUpdateSample = g_generalEnabled && g_rendererProfile.active &&
            g_rendererProfile.updateFrames < kRenderPipelineSampleFrames;
        auto const rendererUpdateStart = rendererUpdateSample ? Clock::now() : Clock::time_point{};

        if (!g_generalEnabled ||
            (!g_diagnosticsEnabled && !g_cullingTelemetryEnabled && !rendererUpdateSample)) {
            PlayLayer::postUpdate(dt);
            recordRenderPipelineUpdate(rendererUpdateSample, rendererUpdateStart, dt);
            finishRendererPipelineSampleIfReady();
            return;
        }
        if (!g_diagnosticsEnabled) {
            PlayLayer::postUpdate(dt);
            recordRenderPipelineUpdate(rendererUpdateSample, rendererUpdateStart, dt);
            finishRendererPipelineSampleIfReady();
            advanceCullingTelemetry();
            return;
        }
        bool const profiling = g_profile.layer == this &&
            g_profile.updateFrames < kFramesToMeasure;
        auto const start = Clock::now();

        if (profiling && g_profile.previousPostUpdate.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                    start - g_profile.previousPostUpdate).count());
            g_profile.maxUpdateGapUs = std::max(g_profile.maxUpdateGapUs, gapUs);
            addGap(g_profile.updateGapBuckets, gapUs);
            g_profile.updateGapSamples.push_back(gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.updateGapsOver50ms;
            if (gapUs > kSevereFrameGapUs) ++g_profile.updateGapsOver100ms;
        }

        PlayLayer::postUpdate(dt);
        recordRenderPipelineUpdate(rendererUpdateSample, rendererUpdateStart, dt);
        finishRendererPipelineSampleIfReady();
        advanceCullingTelemetry();
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
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
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
            if (!g_profile.nodeVisitReported) {
                reportVisitCost("FINAL");
                g_profile.nodeVisitReported = true;
            }
            runFinalSceneDiagnostics();
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
        g_drawSceneHook = registerOptionalHook(self, "::drawScene", "CCDirector::drawScene");
    }

    void drawScene() {
        if (!g_generalEnabled || (!g_diagnosticsEnabled && !g_rendererProfile.active)) {
            CCDirector::drawScene();
            return;
        }
        bool const rendererFrame = g_rendererProfile.active &&
            g_rendererProfile.frames < kRenderPipelineSampleFrames;
        g_rendererProfile.sampleThisFrame = rendererFrame &&
            (g_rendererProfile.frames % kRenderPipelineTimingStride == 0);
        auto const rendererStart = rendererFrame ? Clock::now() : Clock::time_point{};

        bool const profiling = g_diagnosticsEnabled && g_profile.layer != nullptr &&
            g_profile.drawFrames < kFramesToMeasure;
        auto const start = profiling ? Clock::now() : Clock::time_point{};

        if (profiling && g_profile.previousDrawScene.time_since_epoch().count() != 0) {
            auto const gapUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                    start - g_profile.previousDrawScene).count());
            g_profile.maxDrawGapUs = std::max(g_profile.maxDrawGapUs, gapUs);
            addGap(g_profile.drawGapBuckets, gapUs);
            g_profile.drawGapSamples.push_back(gapUs);
            if (gapUs > kLongFrameGapUs) ++g_profile.drawGapsOver50ms;
            if (gapUs > kSevereFrameGapUs) ++g_profile.drawGapsOver100ms;
        }

        CCDirector::drawScene();
        if (rendererFrame) {
            auto const elapsedUs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
                    Clock::now() - rendererStart).count());
            g_rendererProfile.drawSceneTotalUs += elapsedUs;
            g_rendererProfile.drawSceneMaxUs = std::max(g_rendererProfile.drawSceneMaxUs, elapsedUs);
            recordTimingBucket(g_rendererProfile.drawSceneDurationBuckets, elapsedUs);
            if (g_rendererProfile.sampleThisFrame) ++g_rendererProfile.timedDrawFrames;
            ++g_rendererProfile.frames;
            g_rendererProfile.sampleThisFrame = false;
            if (g_rendererProfile.frames % 30 == 0) {
                log::info(
                    "DashCore 0.10.12-alpha RENDER-PIPELINE-PROGRESS: drawSceneFrames={}, postUpdateFrames={}, sampleActive={}.",
                    g_rendererProfile.frames, g_rendererProfile.updateFrames,
                    g_rendererProfile.active
                );
            }
        }
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
            std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
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
                std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 1000000>>>(
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
        // Scene geometry diagnostics are launched from PlayLayer::postUpdate
        // after 600 confirmed gameplay updates; this hook is optional telemetry.
        maybeFinishProfile();
    }
};

$on_mod(Loaded) {
    g_generalEnabled = Mod::get()->getSettingValue<bool>("general-enable");
    g_diagnosticsEnabled = Mod::get()->getSettingValue<bool>("diagnostics-enable");
    g_renderPipelineDiagnosticsRequested =
        Mod::get()->getSettingValue<bool>("render-pipeline-diagnostics");
    g_renderPipelineHooksReady = false;
    g_baseGameLayerUpdateHookReady = false;
    g_playLayerVisibilityHookReady = false;
    g_cullingRequested = Mod::get()->getSettingValue<bool>("experimental-render-culling");
    auto const cullingModePath = Mod::get()->getConfigDir(true) / "static-culling-mode.txt";
    std::ifstream cullingModeFile(cullingModePath.string());
    std::string cullingMode;
    bool const hasCullingModeOverride = static_cast<bool>(cullingModeFile >> cullingMode);
    g_cullingTelemetryEnabled = hasCullingModeOverride;
    if (hasCullingModeOverride) {
        if (cullingMode == "on" || cullingMode == "1") {
            g_cullingRequested = true;
        } else if (cullingMode == "off" || cullingMode == "0") {
            g_cullingRequested = false;
        } else {
            log::warn("DashCore 0.10.12-alpha CULLING-MODE: unrecognized override '{}'; using Geode setting.", cullingMode);
            g_cullingTelemetryEnabled = false;
        }
    }
    log::info(
        "DashCore 0.10.12-alpha CULLING-MODE: mode={}, source={}, config={}.",
        g_cullingRequested ? "ON" : "OFF",
        hasCullingModeOverride ? "file" : "Geode-setting",
        cullingModePath.string()
    );

    // AutoTest enables the expensive scan in a separate launch only.
    // Without this file, the user's Geode setting remains authoritative.
    auto const diagnosticsModePath = Mod::get()->getConfigDir(true) / "diagnostics-mode.txt";
    std::ifstream diagnosticsModeFile(diagnosticsModePath.string());
    std::string diagnosticsMode;
    bool const hasDiagnosticsModeOverride =
        static_cast<bool>(diagnosticsModeFile >> diagnosticsMode);
    if (hasDiagnosticsModeOverride) {
        if (diagnosticsMode == "on" || diagnosticsMode == "1") {
            g_diagnosticsEnabled = true;
        } else if (diagnosticsMode == "off" || diagnosticsMode == "0") {
            g_diagnosticsEnabled = false;
        } else {
            log::warn("DashCore 0.10.12-alpha DIAGNOSTICS-MODE: unrecognized override '{}'; retaining Geode setting.", diagnosticsMode);
        }
    }
    log::info(
        "DashCore 0.10.12-alpha DIAGNOSTICS-MODE: mode={}, source={}, config={}.",
        g_diagnosticsEnabled ? "ON" : "OFF",
        hasDiagnosticsModeOverride ? "file" : "Geode-setting",
        diagnosticsModePath.string()
    );
    auto const renderPipelineModePath = Mod::get()->getConfigDir(true) / "render-pipeline-mode.txt";
    std::ifstream renderPipelineModeFile(renderPipelineModePath.string());
    std::string renderPipelineMode;
    bool const hasRenderPipelineModeOverride =
        static_cast<bool>(renderPipelineModeFile >> renderPipelineMode);
    if (hasRenderPipelineModeOverride) {
        if (renderPipelineMode == "on" || renderPipelineMode == "1") {
            g_renderPipelineDiagnosticsRequested = true;
        } else if (renderPipelineMode == "off" || renderPipelineMode == "0") {
            g_renderPipelineDiagnosticsRequested = false;
        } else {
            log::warn("DashCore 0.10.12-alpha RENDER-PIPELINE-MODE: unrecognized override '{}'; retaining Geode setting.",
                renderPipelineMode);
        }
    }
    log::info(
        "DashCore 0.10.12-alpha RENDER-PIPELINE-MODE: mode={}, source={}, config={}.",
        g_renderPipelineDiagnosticsRequested ? "ON" : "OFF",
        hasRenderPipelineModeOverride ? "file" : "Geode-setting",
        renderPipelineModePath.string()
    );
    log::info("DashCore 0.10.12-alpha GENERAL-ENABLE: {}.", g_generalEnabled ? "ON" : "OFF");
    log::info("DashCore 0.10.12-alpha PERFORMANCE-DIAGNOSTICS: {}.",
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
            log::warn("DashCore 0.10.12-alpha FAST-PATH: unknown mode '{}'; keeping default ON.", requestedMode);
        }
    }
    log::info(
        "DashCore 0.10.12-alpha FAST-PATH: mode={}, source={}, config={}.",
        g_fastPathEnabled ? "ON" : "OFF",
        hasModeOverride ? "file" : "default",
        modePath.string()
    );
    if (g_generalEnabled) {
        if (g_diagnosticsEnabled) {
            bool const nodeReady = enableDiagnosticHook(g_nodeVisitHook, "CCNode::visit");
            bool const postUpdateReady = enableDiagnosticHook(g_postUpdateHook, "PlayLayer::postUpdate");
            bool const drawSceneReady = enableDiagnosticHook(g_drawSceneHook, "CCDirector::drawScene");
            // drawScene is supplementary timing telemetry; the geometry scan
            // is driven by PlayLayer::postUpdate on Android.
            bool const diagnosticsReady = nodeReady && postUpdateReady;
            log::info(
                "DashCore 0.10.12-alpha DIAGNOSTICS-HOOKS: nodeVisit={}, postUpdate={}, drawSceneAvailable={}, fullPassReady={}.",
                nodeReady, postUpdateReady, drawSceneReady, diagnosticsReady
            );
            if (!diagnosticsReady) {
                g_diagnosticsEnabled = false;
                g_cullingRequested = false;
                if (g_nodeVisitHook && g_nodeVisitHook->isEnabled()) {
                    auto const disabled = g_nodeVisitHook->disable();
                    (void)disabled;
                }
                if (g_postUpdateHook && g_postUpdateHook->isEnabled()) {
                    auto const disabled = g_postUpdateHook->disable();
                    (void)disabled;
                }
                if (g_drawSceneHook && g_drawSceneHook->isEnabled()) {
                    auto const disabled = g_drawSceneHook->disable();
                    (void)disabled;
                }
                log::error("DashCore 0.10.12-alpha: geometry diagnostics refused because one or more required hooks could not be enabled; no partial scan will be reported.");
            }
        } else if (g_cullingRequested) {
            bool const nodeReady = enableDiagnosticHook(g_nodeVisitHook, "CCNode::visit");
            bool const telemetryReady = !g_cullingTelemetryEnabled ||
                enableDiagnosticHook(g_postUpdateHook, "PlayLayer::postUpdate");
            if (!nodeReady || !telemetryReady) {
                g_cullingRequested = false;
                g_cullingTelemetryEnabled = false;
                if (g_nodeVisitHook && g_nodeVisitHook->isEnabled()) {
                    auto const disabled = g_nodeVisitHook->disable();
                    (void)disabled;
                }
                if (g_postUpdateHook && g_postUpdateHook->isEnabled()) {
                    auto const disabled = g_postUpdateHook->disable();
                    (void)disabled;
                }
                log::error("DashCore 0.10.12-alpha: experimental culling refused because its required hook(s) could not be enabled.");
            }
        }
    }

    if (g_generalEnabled && g_renderPipelineDiagnosticsRequested) {
        auto ensureHook = [](Hook* hook, char const* label) {
            if (hook && hook->isEnabled()) return true;
            return enableDiagnosticHook(hook, label);
        };
        bool const nodeReady = ensureHook(g_nodeVisitHook, "CCNode::visit (render pipeline)");
        bool const drawSceneReady = ensureHook(g_drawSceneHook, "CCDirector::drawScene (render pipeline)");
        bool const postUpdateReady = ensureHook(g_postUpdateHook, "PlayLayer::postUpdate (render update profile)");
        bool const spriteReady = ensureHook(g_spriteDrawHook, "CCSprite::draw");
        bool const batchReady = ensureHook(g_batchDrawHook, "CCSpriteBatchNode::draw");
        bool const atlasQuadsReady = ensureHook(g_atlasDrawQuadsHook, "CCTextureAtlas::drawQuads");
        bool const atlasCountReady = ensureHook(
            g_atlasDrawNumberOfQuadsHook, "CCTextureAtlas::drawNumberOfQuads"
        );
        // These two update callbacks are optional diagnostics. A missing or
        // unenableable callback must not disable the already validated renderer
        // profile; the report records exactly which subphases were available.
        g_baseGameLayerUpdateHookReady = ensureHook(
            g_baseGameLayerUpdateHook, "GJBaseGameLayer::update (PlayLayer subphase)"
        );
        g_playLayerVisibilityHookReady = ensureHook(
            g_playLayerVisibilityHook, "PlayLayer::updateVisibility (visibility subphase)"
        );
        // drawScene is supplementary: its optional hook can be enabled by
        // Geode without receiving callbacks on the Android runtime. The
        // confirmed postUpdate path owns sample completion.
        g_renderPipelineHooksReady = nodeReady && postUpdateReady &&
            spriteReady && batchReady && atlasQuadsReady && atlasCountReady;
        log::info(
            "DashCore 0.10.12-alpha RENDER-PIPELINE-HOOKS: requested=true, nodeVisit={}, drawScene={}, postUpdate={}, spriteDraw={}, batchDraw={}, atlasDrawQuads={}, atlasDrawNumberOfQuads={}, baseGameLayerUpdate={}, playLayerUpdateVisibility={}, ready={}.",
            nodeReady, drawSceneReady, postUpdateReady, spriteReady, batchReady,
            atlasQuadsReady, atlasCountReady, g_baseGameLayerUpdateHookReady,
            g_playLayerVisibilityHookReady, g_renderPipelineHooksReady
        );
        if (!g_renderPipelineHooksReady) {
            auto disableIfEnabled = [](Hook* hook) {
                if (hook && hook->isEnabled()) {
                    auto const result = hook->disable();
                    (void)result;
                }
            };
            disableIfEnabled(g_spriteDrawHook);
            disableIfEnabled(g_batchDrawHook);
            if (!g_diagnosticsEnabled && !g_cullingTelemetryEnabled) {
                disableIfEnabled(g_postUpdateHook);
            }
            disableIfEnabled(g_atlasDrawQuadsHook);
            disableIfEnabled(g_atlasDrawNumberOfQuadsHook);
            disableIfEnabled(g_baseGameLayerUpdateHook);
            disableIfEnabled(g_playLayerVisibilityHook);
            g_baseGameLayerUpdateHookReady = false;
            g_playLayerVisibilityHookReady = false;
            if (!g_diagnosticsEnabled && !g_cullingRequested) {
                disableIfEnabled(g_nodeVisitHook);
                disableIfEnabled(g_drawSceneHook);
            }
            log::error("DashCore 0.10.12-alpha: render-pipeline diagnostics refused because one or more required hooks could not be enabled; no partial pipeline report will be emitted.");
        }
    }

    if (!g_generalEnabled) {
        log::info("DashCore 0.10.12-alpha: all DashCore hooks remain disabled by General Enable.");
    }
    log::info(
        "DashCore 0.10.12-alpha: render-pipeline diagnostics, performance diagnostics and experimental static-object culling are opt-in."
    );
}
