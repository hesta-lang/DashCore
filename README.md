# OptGod 0.3.0 — Android64 diagnostic profiler

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## What 0.3.0 measures

- Duration of `PlayLayer::init` and `setupHasCompleted`.
- First 180 `PlayLayer::postUpdate` calls after level setup: average/max CPU callback duration, calls over 8 ms, gaps over 25 ms, maximum gap, and observed `dt` range.
- First 180 `CCDirector::drawScene` calls after level setup: average/max CPU duration, calls over 8 ms, gaps over 25 ms, and maximum gap.
- Progress snapshots every 30 samples, plus final reports.

## How to interpret DRAWSCENE-CPU

This times the CPU-side duration of Cocos2d's scene drawing function. It can include scene traversal and graphics-command submission, depending on the engine/driver. It is **not** a direct GPU execution timer and does not prove that the GPU finished rendering during the measured interval. A slow drawScene callback points toward CPU-side rendering/traversal or driver submission; a short callback with visible stutter leaves GPU workload, synchronization, resource upload, and frame pacing as possibilities.

The update and draw samples are independent and each collects up to 180 calls. The profile ends after both complete.

## Safety

This version is diagnostic only. It does not cull objects, skip drawing, change textures/shaders, alter physics, modify triggers, or change gameplay. Timing instrumentation itself has a small measurement overhead.

## Test procedure

1. Install the 0.3.0 artifact.
2. Force-close Geometry Dash, reopen it, and enter a normal level that usually stutters.
3. Play normally for at least 5–8 seconds after the level becomes playable.
4. Export and send the complete Geode log.

Search for `OptGod 0.3.0`. Important final markers:
- `UPDATE FINAL`
- `DRAWSCENE-CPU FINAL`
- `both 180-frame samples complete`

If either final marker is missing, send the full log anyway; partial progress reports are useful.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
