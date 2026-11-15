#include "renderer/geode/RendererCoordinator.hpp"
#if defined(GEODE_IS_ANDROID)
#include "renderer/geode/GlesComputeProbe.hpp"
#include "renderer/geode/GlesDynamicSsboProbe.hpp"
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#if defined(GEODE_IS_ANDROID)
#include <Geode/cocos/platform/android/jni/JniHelper.h>
#include <jni.h>
#include <iomanip>
#include <sstream>
#include <string>
#endif

#include <chrono>
#include <cstdint>

using namespace geode::prelude;

namespace {

#if defined(GEODE_IS_ANDROID)
void runGlesComputeProbeIfEnabled() noexcept {
    static bool attempted = false;
    if (attempted ||
        !Mod::get()->getSettingValue<bool>("gles-compute-diagnostic-probe")) return;

    attempted = true;
    auto const result = dashcore::renderer::geode::runGlesComputeProbe();
    ::geode::log::info(
        "DashCore_GLES_COMPUTE_PROBE status={} es={}.{} max_ssbo_bytes={} dispatched={} output_validated={}",
        dashcore::renderer::geode::glesComputeProbeStatusName(result.status),
        result.majorVersion,
        result.minorVersion,
        result.maxShaderStorageBlockBytes,
        result.dispatchExecuted ? "true" : "false",
        result.outputValidated ? "true" : "false"
    );
}

void runGlesDynamicSsboProbeIfEnabled() noexcept {
    static bool attempted = false;
    if (attempted ||
        !Mod::get()->getSettingValue<bool>("gles-dynamic-ssbo-diagnostic-probe")) return;

    attempted = true;
    auto const result = dashcore::renderer::geode::runGlesDynamicSsboProbe();
    ::geode::log::info(
        "DashCore_GLES_DYNAMIC_SSBO_PROBE status={} es={}.{} max_ssbo_bytes={} batches={} reallocations={} dispatches={} upload_bytes={} readback_bytes={} max_buffer_bytes={} validated={}",
        dashcore::renderer::geode::glesDynamicSsboProbeStatusName(result.status),
        result.majorVersion,
        result.minorVersion,
        result.maxShaderStorageBlockBytes,
        result.completedBatches,
        result.bufferStorageReallocations,
        result.dispatches,
        result.uploadBytesSubmitted,
        result.readbackBytesMapped,
        result.maxBufferBytes,
        result.allBatchesValidated ? "true" : "false"
    );
}

double nanosecondsToMilliseconds(std::uint64_t nanoseconds) noexcept {
    return static_cast<double>(nanoseconds) / 1000000.0;
}

std::string makeLauncherDiagnosticsJson(
    dashcore::renderer::core::RendererDiagnosticSummary const& report
) {
    auto const& counters = report.totals;
    std::ostringstream json;
    json << std::fixed << std::setprecision(3)
         << "{"
         << "\"frames\":" << report.totalFrames
         << ",\"successful_frames\":" << report.successfulFrames
         << ",\"failed_frames\":" << report.failedFrames
         << ",\"capture_enabled\":" << (report.captureEnabled ? "true" : "false")
         << ",\"capture_performed_frames\":" << report.capturePerformedFrames
         << ",\"capture_skipped_frames\":" << report.captureSkippedFrames
         << ",\"index_build_total_ms\":" << nanosecondsToMilliseconds(report.totalIndexBuildNanoseconds)
         << ",\"post_update_avg_ms\":" << nanosecondsToMilliseconds(report.averagePostUpdateNanoseconds)
         << ",\"post_update_p95_last512_ms\":" << nanosecondsToMilliseconds(report.p95PostUpdateNanoseconds)
         << ",\"post_update_max_ms\":" << nanosecondsToMilliseconds(report.maximumPostUpdateNanoseconds)
         << ",\"capture_avg_ms\":" << nanosecondsToMilliseconds(report.averageCaptureNanoseconds)
         << ",\"capture_p95_last512_ms\":" << nanosecondsToMilliseconds(report.p95CaptureNanoseconds)
         << ",\"capture_max_ms\":" << nanosecondsToMilliseconds(report.maximumCaptureNanoseconds)
         << ",\"collection_avg_ms\":" << nanosecondsToMilliseconds(report.averageCollectionNanoseconds)
         << ",\"collection_p95_last512_ms\":" << nanosecondsToMilliseconds(report.p95CollectionNanoseconds)
         << ",\"collection_max_ms\":" << nanosecondsToMilliseconds(report.maximumCollectionNanoseconds)
         << ",\"p95_window_frames\":" << report.timingWindowSamples
         << ",\"nodes_visited_total\":" << counters.nodesVisited
         << ",\"object_array_index_builds_total\":" << counters.objectArrayIndexBuilds
         << ",\"object_array_entries_indexed_total\":" << counters.objectArrayEntries
         << ",\"objects_cached_in_playlayer_tree_total\":" << counters.objectsInPlayLayerTree
         << ",\"objects_excluded_from_playlayer_tree_index_total\":" << counters.objectsOutsidePlayLayerTree
         << ",\"root_objects_total\":" << counters.rootObjects
         << ",\"sprite_candidates_total\":" << counters.candidateSprites
         << ",\"sprites_accepted_total\":" << counters.acceptedSprites
         << ",\"invisible_subtrees_total\":" << counters.invisibleSubtrees
         << ",\"rejected_flags_total\":" << counters.rejectedFlags
         << ",\"rejected_batched_total\":" << counters.rejectedBatched
         << ",\"sprites_skipped_unsupported_flags_total\":" << counters.rejectedFlags
         << ",\"sprites_skipped_batch_node_total\":" << counters.rejectedBatched
         << ",\"rejected_missing_texture_total\":" << counters.rejectedMissingTexture
         << ",\"rejected_unuploaded_texture_total\":" << counters.rejectedUnuploadedTexture
         << ",\"rejected_blend_total\":" << counters.rejectedBlend
         << ",\"rejected_quad_total\":" << counters.rejectedQuad
         << ",\"rejected_transform_total\":" << counters.rejectedTransform
         << ",\"rejected_descriptor_total\":" << counters.rejectedDescriptor
         << ",\"duplicate_node_refs_total\":" << counters.duplicateNodeReferences
         << ",\"depth_limit_hits_total\":" << counters.depthLimitHits
         << ",\"node_limit_hits_total\":" << counters.nodeLimitHits
         << ",\"children_limit_hits_total\":" << counters.childrenLimitHits
         << ",\"source_failures_total\":" << counters.sourceFailures
         << ",\"unique_textures_last\":" << report.lastUniqueTextureCount
         << ",\"unique_textures_peak\":" << report.maximumUniqueTextureCount
         << ",\"snapshot_sprites_last\":" << report.lastSnapshotSprites
         << ",\"snapshot_sprites_peak\":" << report.maximumSnapshotSprites
         << ",\"snapshot_vector_capacity_bytes_last\":" << report.lastSnapshotCapacityBytes
         << ",\"snapshot_vector_capacity_bytes_peak\":" << report.maximumSnapshotCapacityBytes
         << ",\"memory_sample_count\":" << report.memorySampleCount
         << ",\"system_ram_total_bytes\":" << report.memory.systemTotalBytes
         << ",\"system_ram_available_bytes\":" << report.memory.systemAvailableBytes
         << ",\"system_memory_valid\":" << (report.memory.hasSystemMemory ? "true" : "false")
         << ",\"process_rss_bytes\":" << report.memory.processRssBytes
         << ",\"process_rss_valid\":" << (report.memory.hasProcessRss ? "true" : "false")
         << ",\"process_pss_bytes\":" << report.memory.processPssBytes
         << ",\"process_pss_valid\":" << (report.memory.hasProcessPss ? "true" : "false")
         << ",\"process_peak_rss_bytes\":" << report.memory.processPeakRssBytes
         << ",\"process_peak_rss_valid\":" << (report.memory.hasProcessPeakRss ? "true" : "false")
         << ",\"process_virtual_bytes\":" << report.memory.processVirtualBytes
         << ",\"process_virtual_valid\":" << (report.memory.hasProcessVirtual ? "true" : "false")
         << ",\"cgroup_memory_limit_bytes\":" << report.memory.cgroupMemoryLimitBytes
         << ",\"cgroup_memory_limit_valid\":" << (report.memory.hasCgroupMemoryLimit ? "true" : "false")
         << ",\"cgroup_memory_limit_unlimited\":" << (report.memory.cgroupMemoryLimitUnlimited ? "true" : "false")
         << ",\"cgroup_memory_current_bytes\":" << report.memory.cgroupMemoryCurrentBytes
         << ",\"cgroup_memory_current_valid\":" << (report.memory.hasCgroupMemoryCurrent ? "true" : "false")
         << ",\"rlimit_as_soft_bytes\":" << report.memory.resourceLimits.addressSpace.softBytes
         << ",\"rlimit_as_hard_bytes\":" << report.memory.resourceLimits.addressSpace.hardBytes
         << ",\"rlimit_as_valid\":" << (report.memory.resourceLimits.addressSpace.available ? "true" : "false")
         << ",\"rlimit_as_soft_unlimited\":" << (report.memory.resourceLimits.addressSpace.softUnlimited ? "true" : "false")
         << ",\"rlimit_as_hard_unlimited\":" << (report.memory.resourceLimits.addressSpace.hardUnlimited ? "true" : "false")
         << ",\"rlimit_rss_soft_bytes\":" << report.memory.resourceLimits.residentSet.softBytes
         << ",\"rlimit_rss_hard_bytes\":" << report.memory.resourceLimits.residentSet.hardBytes
         << ",\"rlimit_rss_valid\":" << (report.memory.resourceLimits.residentSet.available ? "true" : "false")
         << ",\"rlimit_rss_soft_unlimited\":" << (report.memory.resourceLimits.residentSet.softUnlimited ? "true" : "false")
         << ",\"rlimit_rss_hard_unlimited\":" << (report.memory.resourceLimits.residentSet.hardUnlimited ? "true" : "false")
         << ",\"memory_safety_reserve_bytes\":" << report.memoryBudget.systemSafetyReserveBytes
         << ",\"memory_headroom_bytes\":" << report.memoryBudget.availableHeadroomBytes
         << ",\"memory_budget_bytes\":" << report.memoryBudget.budgetBytes
         << ",\"memory_max_single_buffer_bytes\":" << report.memoryBudget.maxSingleBufferBytes
         << ",\"memory_budget_can_allocate\":" << (report.memoryBudget.canAllocateBuffers ? "true" : "false")
         << ",\"memory_scope\":\"procfs_cgroup_v1_v2_rlimit_soft_hard_read_only_diagnostics\""
         << "}";
    return json.str();
}

void clearPendingJniException() noexcept {
    auto vm = cocos2d::JniHelper::getJavaVM();
    JNIEnv* env = nullptr;
    if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK &&
        env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void notifyLauncherLevelStarted() noexcept {
    cocos2d::JniMethodInfo method{};
    auto const found = cocos2d::JniHelper::getStaticMethodInfo(
        method,
        "com/geode/launcher/LauncherDiagnostics",
        "onLevelStarted",
        "(Ljava/lang/String;Ljava/lang/String;)V"
    );

    if (!found) {
        clearPendingJniException();
        return;
    }

    // The first bridge records exact PlayLayer boundaries; level identity is omitted.
    method.env->CallStaticVoidMethod(
        method.classID,
        method.methodID,
        static_cast<jstring>(nullptr),
        static_cast<jstring>(nullptr)
    );
    if (method.env->ExceptionCheck()) method.env->ExceptionClear();
    method.env->DeleteLocalRef(method.classID);
}

void notifyLauncherLevelEnded() noexcept {
    cocos2d::JniMethodInfo method{};
    auto const found = cocos2d::JniHelper::getStaticMethodInfo(
        method,
        "com/geode/launcher/LauncherDiagnostics",
        "onLevelEnded",
        "()V"
    );

    if (!found) {
        clearPendingJniException();
        return;
    }

    method.env->CallStaticVoidMethod(method.classID, method.methodID);
    if (method.env->ExceptionCheck()) method.env->ExceptionClear();
    method.env->DeleteLocalRef(method.classID);
}

void notifyLauncherLevelDiagnostics(
    dashcore::renderer::core::RendererDiagnosticSummary const& report
) noexcept {
    if (report.totalFrames == 0) return;

    cocos2d::JniMethodInfo method{};
    auto const found = cocos2d::JniHelper::getStaticMethodInfo(
        method,
        "com/geode/launcher/LauncherDiagnostics",
        "onDashCoreLevelDiagnostics",
        "(Ljava/lang/String;)V"
    );

    if (!found) {
        clearPendingJniException();
        return;
    }

    auto const payload = makeLauncherDiagnosticsJson(report);
    auto const value = method.env->NewStringUTF(payload.c_str());
    if (!value) {
        if (method.env->ExceptionCheck()) method.env->ExceptionClear();
        method.env->DeleteLocalRef(method.classID);
        return;
    }

    method.env->CallStaticVoidMethod(method.classID, method.methodID, value);
    if (method.env->ExceptionCheck()) method.env->ExceptionClear();
    method.env->DeleteLocalRef(value);
    method.env->DeleteLocalRef(method.classID);
}
#else
void runGlesComputeProbeIfEnabled() noexcept {}
void runGlesDynamicSsboProbeIfEnabled() noexcept {}
void notifyLauncherLevelStarted() noexcept {}
void notifyLauncherLevelEnded() noexcept {}
#endif

} // namespace

class $modify(DashCoreExperimentalPlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        auto const captureEnabled =
            Mod::get()->getSettingValue<bool>("capture-object-diagnostics");
        dashcore::renderer::geode::coordinator().onLevelInit(this, captureEnabled);
        notifyLauncherLevelStarted();
    }

    void postUpdate(float dt) {
        auto const updateStart = std::chrono::steady_clock::now();
        PlayLayer::postUpdate(dt);
        auto const updateElapsed = std::chrono::steady_clock::now() - updateStart;
        auto const updateNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(updateElapsed).count()
        );
        dashcore::renderer::geode::coordinator().onFrame(this, updateNanoseconds);
        runGlesComputeProbeIfEnabled();
        runGlesDynamicSsboProbeIfEnabled();
    }

    void onExit() {
        auto& renderCoordinator = dashcore::renderer::geode::coordinator();
        if (renderCoordinator.active()) {
            notifyLauncherLevelEnded();
#if defined(GEODE_IS_ANDROID)
            auto const report = renderCoordinator.summary();
            notifyLauncherLevelDiagnostics(report);
#endif
            renderCoordinator.onLevelExit();
        }
        PlayLayer::onExit();
    }
};
