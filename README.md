# OptGod — Android64 review build

Diagnostic-only review build for Geometry Dash 2.2081 / Geode 5.11.2.

## What this build measures

- Duration of `PlayLayer::init`.
- Duration of `setupHasCompleted`.
- Duration of the first 180 `PlayLayer::postUpdate` calls after setup.
- Average and maximum post-update time, count of post-updates over 8 ms, gaps over 25 ms, maximum gap, and the observed `dt` range.
- Progress snapshots every 30 post-update calls, so a partial run still produces useful measurements.

## Instrumentation fix in 0.2.1

The previous build cleared the profile state after calling the original `PlayLayer::init`. Since `setupHasCompleted` can run from inside that initialization, this erased the active PlayLayer pointer before frame sampling began. Version 0.2.1 clears old state before calling the original init and samples `PlayLayer::postUpdate`, rather than relying on a PlayLayer `update` override that may not be dispatched on this game version.

## Important

This build does **not** enable culling, skip objects, change rendering, or modify gameplay. It is intended to establish whether post-update processing and frame pacing are associated with level-entry stutter before a behavior-changing optimization is attempted. The post-update duration is not a complete GPU/render-thread measurement; the frame-gap metric captures spacing between observed post-update calls.

## Test procedure

1. Install this build on the target device.
2. Force-close Geometry Dash, reopen it, and enter a normal level that usually stutters.
3. Play normally for at least 5 seconds after the level becomes playable.
4. Export the Geode log and send the complete log, not only the lines containing OptGod.

Search the log for `OptGod REVIEW`. Expected markers include `first PlayLayer::postUpdate reached`, progress reports at 30, 60, 90, 120, 150 frames, and a final report at 180 frames. If setup is logged but the first post-update marker is absent, the next investigation should move to a different verified gameplay callback.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build through GitHub Actions
