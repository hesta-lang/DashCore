# DashCore 0.5.3 — Android64 profiler + scene-graph diagnostics

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## What's new in 0.5.3

This release pauses the experimental render-culling action because the previous Android test showed that the `CCNode::visit` hook only observed a very small number of GameObjects (about one per rendered frame). That is not enough evidence to safely skip any object's drawing.

DashCore now performs a separate, read-only traversal of the active `PlayLayer` child tree every 30 draw samples. It reports:

- `nodes`: total Cocos nodes found in the PlayLayer subtree.
- `gameObjects`: GameObject instances found by Geode runtime type information.
- `effects`: Effect-class GameObjects.
- `grouped`: GameObjects with group membership.
- `hiddenGameObjects`: GameObjects already marked invisible by the game.
- `maxChildren` and `maxDepth`: shape of the scene tree.
- `lastScan`: CPU time spent on the most recent read-only scan.
- `dontCreateObjects`: the flag passed to PlayLayer::init for this session.

This inspection does not modify visibility, transforms, scheduling, triggers, object state, or rendering.

## Experimental culling status

The setting remains present for compatibility, but culling is **temporarily forced off**, even if the setting is enabled. The log distinguishes:

- `requested=true/false`: the user's saved setting.
- `active=false`: actual culling state in 0.5.3.
- `visited`: GameObjects encountered by the CCNode::visit hook.
- `culled=0`: expected while culling is suspended.

Do not use 0.5.3 to compare culling performance. Its purpose is to establish whether the PlayLayer tree contains all level objects and whether the current visit hook sees the same objects. Culling will only be re-enabled after that detection is validated on Android.

## Profiler

- Duration of `PlayLayer::init` and `setupHasCompleted`.
- First 180 `PlayLayer::postUpdate` calls after level setup: average/max CPU callback duration, calls over 8 ms, frame gaps over 25 ms, maximum gap, observed `dt` range, and duration/gap distributions.
- First 180 `CCDirector::drawScene` calls after level setup: average/max CPU duration, calls over 8 ms, frame gaps over 25 ms, maximum gap, and duration/gap distributions.
- Read-only scene-graph snapshots every 30 draw samples.
- Progress snapshots every 30 samples, plus final reports.

Duration buckets: <=2 ms, 2–4 ms, 4–8 ms, 8–16 ms, 16–25 ms, and >25 ms.

Frame-gap buckets: <=16.67 ms, 16.67–25 ms, 25–50 ms, 50–100 ms, and >100 ms.

### How to interpret DRAWSCENE-CPU

This times the CPU-side duration of Cocos2d's scene drawing function. It can include scene traversal and graphics-command submission, depending on the engine/driver. It is **not** a direct GPU execution timer and does not prove that the GPU finished rendering during the measured interval.

## Test procedure

1. Install the 0.5.3 artifact.
2. Start a normal level from the level's Play button (not the editor's preview/test mode).
3. Let it run for at least 5–10 seconds so the 180-frame sample completes.
4. Send the complete Geode log, especially the `DashCore 0.5.3 SCENE-GRAPH`, `CULL FINAL`, and `PlayLayer::init` lines.
5. Make sure the log shows `dontCreateObjects=false` for the normal gameplay test. If it is true, tell me exactly how the level was launched.

For a fair profiler comparison, use the same level, StartPos, graphics settings, and approximate test duration. Avoid comparing a menu/transition-heavy run with steady gameplay.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
