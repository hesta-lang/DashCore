# OptGod 0.5.0 — Android64 profiler + experimental render culling

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## Experimental render-culling prototype

This is the first cautious step toward chunk-style rendering. It does **not** unload level objects or change when gameplay logic runs. All level objects are still created and remain in the game.

When **Experimental render culling** is enabled in Geode mod settings, OptGod skips only the Cocos draw traversal for a GameObject when all of these conditions are true:

- It is an ordinary non-Effect object.
- It has no group membership (grouped objects can move, animate, or be linked to other objects).
- Its transformed bounds are farther than a wide 0.75-screen guard band from the visible window.

**Triggers are protected:** Effect-class objects are never culled. Their draw traversal and all trigger execution remain untouched. Grouped objects are also preserved. This version does not execute triggers early; pre-executing them could change gameplay and is not safe.

The feature is **disabled by default**. Enable it only for testing, and turn it off if you notice missing objects, visual differences, or unusual behavior. This is a render-culling prototype, not yet true chunk loading. It is intended to test whether reducing distant static-object draw traversal improves frame pacing.

Important limitations:
- It does not reduce initial object creation or level parsing time.
- It does not unload memory or texture resources.
- It deliberately leaves triggers, Effect objects, and grouped objects alone.
- It may offer limited gains on levels where most objects are grouped or animated.
- The wide guard band is intended to reduce pop-in, but needs testing on camera movement, zoom, and rotated gameplay.

## Profiler

- Duration of `PlayLayer::init` and `setupHasCompleted`.
- First 180 `PlayLayer::postUpdate` calls after level setup: average/max CPU callback duration, calls over 8 ms, frame gaps over 25 ms, maximum gap, observed `dt` range, and duration/gap distributions.
- First 180 `CCDirector::drawScene` calls after level setup: average/max CPU duration, calls over 8 ms, frame gaps over 25 ms, maximum gap, and duration/gap distributions.
- Progress snapshots every 30 samples, plus final reports.

Duration buckets: <=2 ms, 2–4 ms, 4–8 ms, 8–16 ms, 16–25 ms, and >25 ms.

Frame-gap buckets: <=16.67 ms, 16.67–25 ms, 25–50 ms, 50–100 ms, and >100 ms.

### How to interpret DRAWSCENE-CPU

This times the CPU-side duration of Cocos2d's scene drawing function. It can include scene traversal and graphics-command submission, depending on the engine/driver. It is **not** a direct GPU execution timer and does not prove that the GPU finished rendering during the measured interval.

## Test procedure

1. Install the 0.5.0 artifact.
2. First test with **Experimental render culling disabled** to verify the profiler/build behaves normally.
3. Enable the option in Geode settings and restart the level.
4. Test the same normal level and a level with StartPos. Watch for missing objects, pop-in, trigger differences, or camera/zoom issues.
5. Export and send the complete Geode log, especially the `OptGod 0.5.0` timing reports.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
