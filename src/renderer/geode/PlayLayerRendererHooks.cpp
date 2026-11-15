#include "renderer/geode/RendererCoordinator.hpp"
#include "renderer/core/ObjectGrouping.hpp"
#if defined(GEODE_IS_ANDROID)
#include "renderer/geode/GlesComputeProbe.hpp"
#include "renderer/geode/GlesDynamicSsboProbe.hpp"
#include "renderer/geode/GlesSsboTransferBenchmark.hpp"
#include "renderer/geode/GlesSsboAsyncTransferBenchmark.hpp"
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <chrono>
#include <cstdint>

using namespace geode::prelude;

namespace {

void runCpuObjectGroupingDiagnosticIfEnabled() noexcept {
    static bool attempted = false;
    if (attempted ||
        !Mod::get()->getSettingValue<bool>("cpu-object-grouping-diagnostic")) {
        return;
    }

    attempted = true;
    auto const result =
        dashcore::renderer::core::runSyntheticObjectGroupingDiagnostic();
    ::geode::log::info(
        "DashCore_CPU_OBJECT_GROUPING status={} synthetic=true objects={} grouped_objects={} groups={} packed_bytes={} group_table_bytes={} elapsed_ms={} validated={}",
        dashcore::renderer::core::objectGroupingStatusName(result.status),
        result.inputObjects,
        result.groupedObjects,
        result.groupCount,
        result.packedDataBytes,
        result.groupRangeBytes,
        static_cast<double>(result.elapsedNanoseconds) / 1000000.0,
        result.validated ? "true" : "false"
    );
}

#if defined(GEODE_IS_ANDROID)
bool g_asyncSsboBenchmarkRequested = false;

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


void runGlesSsboAsyncTransferBenchmarkIfEnabled() noexcept {
    static bool logged = false;
    if (logged) return;

    auto const enabled =
        Mod::get()->getSettingValue<bool>("gles-ssbo-async-transfer-benchmark");
    if (enabled) g_asyncSsboBenchmarkRequested = true;
    auto const result =
        dashcore::renderer::geode::tickGlesSsboAsyncTransferBenchmark(enabled);
    if (result.status == dashcore::renderer::geode::GlesSsboAsyncTransferStatus::NotStarted ||
        result.status == dashcore::renderer::geode::GlesSsboAsyncTransferStatus::Running) {
        return;
    }

    logged = true;
    for (std::uint32_t index = 0; index < result.sampleCount; ++index) {
        auto const& sample = result.samples[index];
        ::geode::log::info(
            "DashCore_GLES_SSBO_ASYNC_BENCHMARK size_bytes={} repeats={} allocation_ms={} upload_avg_ms={} compute_submit_avg_ms={} fence_latency_avg_ms={} fence_latency_max_ms={} fence_poll_frames_avg={} fence_poll_frames_max={} readback_avg_ms={} end_to_end_avg_ms={}",
            sample.payloadBytes,
            sample.completedRepeats,
            sample.allocationMs,
            sample.uploadAverageMs,
            sample.computeSubmitAverageMs,
            sample.fenceLatencyAverageMs,
            sample.fenceLatencyMaximumMs,
            sample.averageFencePollFrames,
            sample.maximumFencePollFrames,
            sample.readbackAverageMs,
            sample.endToEndAverageMs
        );
    }
    ::geode::log::info(
        "DashCore_GLES_SSBO_ASYNC_BENCHMARK_SUMMARY status={} es={}.{} payload_sizes={} reallocations={} dispatches={} upload_bytes={} readback_bytes={} max_buffer_bytes={} fence_poll_calls={} pending_polls={} max_poll_frames={} validated={}",
        dashcore::renderer::geode::glesSsboAsyncTransferStatusName(result.status),
        result.majorVersion,
        result.minorVersion,
        result.completedPayloadSizes,
        result.bufferStorageReallocations,
        result.dispatches,
        result.uploadBytesSubmitted,
        result.readbackBytesMapped,
        result.maxBufferBytes,
        result.fencePollCalls,
        result.pendingFencePolls,
        result.maximumFencePollFrames,
        result.allPayloadSizesValidated ? "true" : "false"
    );
}

void runGlesSsboTransferBenchmarkIfEnabled() noexcept {
    static bool attempted = false;
    if (attempted ||
        g_asyncSsboBenchmarkRequested ||
        Mod::get()->getSettingValue<bool>("gles-ssbo-async-transfer-benchmark") ||
        !Mod::get()->getSettingValue<bool>("gles-ssbo-legacy-sync-benchmark")) return;

    attempted = true;
    auto const result = dashcore::renderer::geode::runGlesSsboTransferBenchmark();
    for (std::uint32_t index = 0; index < result.sampleCount; ++index) {
        auto const& sample = result.samples[index];
        ::geode::log::info(
            "DashCore_GLES_SSBO_BENCHMARK size_bytes={} repeats={} allocation_ms={} upload_avg_ms={} upload_min_ms={} upload_max_ms={} compute_submit_avg_ms={} compute_submit_min_ms={} compute_submit_max_ms={} readback_avg_ms={} readback_min_ms={} readback_max_ms={} cycle_avg_ms={} cycle_min_ms={} cycle_max_ms={}",
            sample.payloadBytes,
            sample.completedRepeats,
            sample.allocationMs,
            sample.uploadAverageMs,
            sample.uploadMinimumMs,
            sample.uploadMaximumMs,
            sample.computeSubmitAverageMs,
            sample.computeSubmitMinimumMs,
            sample.computeSubmitMaximumMs,
            sample.readbackAverageMs,
            sample.readbackMinimumMs,
            sample.readbackMaximumMs,
            sample.cycleAverageMs,
            sample.cycleMinimumMs,
            sample.cycleMaximumMs
        );
    }
    ::geode::log::info(
        "DashCore_GLES_SSBO_BENCHMARK_SUMMARY status={} es={}.{} payload_sizes={} reallocations={} dispatches={} upload_bytes={} readback_bytes={} max_buffer_bytes={} validated={}",
        dashcore::renderer::geode::glesSsboTransferBenchmarkStatusName(result.status),
        result.majorVersion,
        result.minorVersion,
        result.completedPayloadSizes,
        result.bufferStorageReallocations,
        result.dispatches,
        result.uploadBytesSubmitted,
        result.readbackBytesMapped,
        result.maxBufferBytes,
        result.allPayloadSizesValidated ? "true" : "false"
    );
}

#else
void runGlesComputeProbeIfEnabled() noexcept {}
void runGlesDynamicSsboProbeIfEnabled() noexcept {}
void runGlesSsboTransferBenchmarkIfEnabled() noexcept {}
void runGlesSsboAsyncTransferBenchmarkIfEnabled() noexcept {}
#endif

} // namespace

class $modify(DashCoreExperimentalPlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        auto const captureEnabled =
            Mod::get()->getSettingValue<bool>("capture-object-diagnostics");
        dashcore::renderer::geode::coordinator().onLevelInit(this, captureEnabled);
    }

    void postUpdate(float dt) {
        auto const updateStart = std::chrono::steady_clock::now();
        PlayLayer::postUpdate(dt);
        auto const updateElapsed = std::chrono::steady_clock::now() - updateStart;
        auto const updateNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(updateElapsed).count()
        );
        dashcore::renderer::geode::coordinator().onFrame(this, updateNanoseconds);
        runCpuObjectGroupingDiagnosticIfEnabled();
        runGlesComputeProbeIfEnabled();
        runGlesDynamicSsboProbeIfEnabled();
        runGlesSsboAsyncTransferBenchmarkIfEnabled();
        runGlesSsboTransferBenchmarkIfEnabled();
    }

    void onExit() {
        auto& renderCoordinator = dashcore::renderer::geode::coordinator();
        if (renderCoordinator.active()) {
            renderCoordinator.onLevelExit();
        }
#if defined(GEODE_IS_ANDROID)
        dashcore::renderer::geode::cancelGlesSsboAsyncTransferBenchmark();
#endif
        PlayLayer::onExit();
    }
};
