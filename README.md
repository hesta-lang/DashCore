# DashCore 0.5.2 — Android64 profiler + render-culling instrumentation

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## What's new in 0.5.2

This release fixes the render hook: GameObjects inherit Cocos2d's visit implementation, so hooking GameObject::visit did not reliably intercept drawing. The mod now hooks cocos2d::CCNode::visit and uses Geode runtime type information to identify GameObject instances before applying culling.

During the first 180 drawScene samples after level setup, the log reports:

- `enabled`: whether Experimental render culling was enabled when the level setup completed.
- `visited`: GameObject instances encountered through CCNode::visit during the measurement window.
- `eligible`: visited objects that are neither Effect-class nor grouped.
- `protectedEffect`: Effect-class objects that are deliberately preserved.
- `protectedGrouped`: objects with group membership that are deliberately preserved.
- `boundsChecks`: eligible objects for which the distance/bounds test was attempted.
- `culled`: draw traversals actually skipped by the prototype.

The counters are cumulative across the measurement window, not unique-object counts. An object can be visited more than once, so these values must not be interpreted as the total number of distinct objects in a level.

## Experimental render-culling prototype

This is a cautious first step toward chunk-style rendering. It does **not** unload level objects or change when gameplay logic runs. All level objects are still created and remain in the game.

When **Experimental render culling** is enabled in Geode mod settings, DashCore skips only the Cocos draw traversal for a GameObject when all of these conditions are true:

- It is an ordinary non-Effect object.
- It has no group membership.
- Its transformed bounds are farther than a wide 0.75-screen guard band from the visible window.

Effect-class objects (including trigger-class objects) and grouped objects are never culled. Trigger execution is not advanced, delayed, or otherwise modified.

The feature is **disabled by default**. Keep it disabled for the baseline test. Enable it only for the comparison test, and turn it off if you notice missing objects, pop-in, visual differences, or unusual behavior. This is render culling, not true chunk loading.

Important limitations:
- It does not reduce initial object creation or level parsing time.
- It does not unload memory or texture resources.
- It deliberately leaves Effect objects and grouped objects alone.
- It may offer limited gains on levels where most objects are grouped or animated.
- The guard band needs testing with camera movement, zoom, rotated gameplay, and moving objects.
- The counters count visits, not unique objects.

## Profiler

- Duration of `PlayLayer::init` and `setupHasCompleted`.
- First 180 `PlayLayer::postUpdate` calls after level setup: average/max CPU callback duration, calls over 8 ms, frame gaps over 25 ms, maximum gap, observed `dt` range, and duration/gap distributions.
- First 180 `CCDirector::drawScene` calls after level setup: average/max CPU duration, calls over 8 ms, frame gaps over 25 ms, maximum gap, and duration/gap distributions.
- Progress snapshots every 30 samples, plus final reports.
- Render-culling state and object counters in `DashCore 0.5.2 CULL FINAL`.

Duration buckets: <=2 ms, 2–4 ms, 4–8 ms, 8–16 ms, 16–25 ms, and >25 ms.

Frame-gap buckets: <=16.67 ms, 16.67–25 ms, 25–50 ms, 50–100 ms, and >100 ms.

### How to interpret DRAWSCENE-CPU

This times the CPU-side duration of Cocos2d's scene drawing function. It can include scene traversal and graphics-command submission, depending on the engine/driver. It is **not** a direct GPU execution timer and does not prove that the GPU finished rendering during the measured interval.

## Test procedure

1. Install the 0.5.2 artifact.
2. Run a baseline test with **Experimental render culling disabled**. The log should say `experimental-render-culling=false` and `CULL FINAL enabled=false`.
3. Restart the same level from the same position, enable the option, and run the same test again. The log should say `experimental-render-culling=true`.
4. Compare `DRAWSCENE-CPU FINAL` and `CULL FINAL` between the two runs.
5. Check for missing objects, pop-in, trigger differences, or camera/zoom issues. Disable culling immediately if anything behaves incorrectly.
6. Send the complete Geode log, especially the `DashCore 0.5.2` lines.

For a fair comparison, use the same level, StartPos, graphics settings, and approximate test duration. Avoid comparing a menu/transition-heavy run with a steady gameplay run.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
