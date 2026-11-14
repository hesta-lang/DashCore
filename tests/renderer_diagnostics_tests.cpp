#include "renderer/core/RendererDiagnostics.hpp"

#include <cassert>
#include <cstddef>

using namespace dashcore::renderer::core;

int main() {
    RendererDiagnostics diagnostics;

    CaptureFrameSample first{};
    first.succeeded = true;
    first.captureNanoseconds = 100;
    first.collectionNanoseconds = 40;
    first.snapshotSprites = 3;
    first.snapshotCapacityBytes = 480;
    first.counters.nodesVisited = 10;
    first.counters.candidateSprites = 4;
    first.counters.acceptedSprites = 3;
    first.counters.uniqueTextures = 2;
    diagnostics.record(first);

    CaptureFrameSample second{};
    second.succeeded = false;
    second.captureNanoseconds = 300;
    second.collectionNanoseconds = 100;
    second.counters.nodesVisited = 20;
    second.counters.rejectedQuad = 1;
    second.counters.uniqueTextures = 1;
    diagnostics.record(second);

    auto const summary = diagnostics.summary();
    assert(summary.totalFrames == 2);
    assert(summary.successfulFrames == 1);
    assert(summary.failedFrames == 1);
    assert(summary.totals.nodesVisited == 30);
    assert(summary.totals.candidateSprites == 4);
    assert(summary.totals.acceptedSprites == 3);
    assert(summary.totals.rejectedQuad == 1);
    assert(summary.averageCaptureNanoseconds == 200);
    assert(summary.p95CaptureNanoseconds == 300);
    assert(summary.averageCollectionNanoseconds == 70);
    assert(summary.p95CollectionNanoseconds == 100);
    assert(summary.timingWindowSamples == 2);
    assert(summary.lastSnapshotSprites == 0);
    assert(summary.maximumSnapshotSprites == 3);
    assert(summary.maximumSnapshotCapacityBytes == 480);
    assert(summary.lastUniqueTextureCount == 1);
    assert(summary.maximumUniqueTextureCount == 2);

    RendererDiagnostics boundedWindow;
    CaptureFrameSample oldSample{};
    oldSample.captureNanoseconds = 1;
    boundedWindow.record(oldSample);
    CaptureFrameSample recentSample{};
    recentSample.captureNanoseconds = 10;
    for (std::size_t index = 0; index < RendererDiagnostics::kTimingSampleWindow; ++index) {
        boundedWindow.record(recentSample);
    }
    auto const boundedSummary = boundedWindow.summary();
    assert(boundedSummary.totalFrames == 513);
    assert(boundedSummary.timingWindowSamples == 512);
    assert(boundedSummary.p95CaptureNanoseconds == 10);
    assert(boundedSummary.maximumCaptureNanoseconds == 10);
}
