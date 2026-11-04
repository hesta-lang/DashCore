# OptGod — Android64 review build

Diagnostic-only review build for Geometry Dash 2.2081 / Geode 5.11.2.

## What this build measures

- Duration of `PlayLayer::init`.
- Duration of `setupHasCompleted`.
- Duration of the first 180 `PlayLayer::update` calls after setup.
- Average and maximum update time, count of updates over 8 ms, gaps over 25 ms, maximum gap, and the observed `dt` range.
- Progress snapshots every 30 update calls, so a partial run still produces useful measurements.

## Important

This build does **not** enable culling, skip objects, change rendering, or modify gameplay. It is intended to establish whether the main update loop is where the level-entry stutter is occurring before a behavior-changing optimization is attempted.

## Test procedure

1. Install this build on the target device.
2. Force-close Geometry Dash, reopen it, and enter a normal level that usually stutters.
3. Let it run for at least 5 seconds after the level becomes playable.
4. Repeat once with a StartPos if useful; keep the same level and conditions.
5. Export the Geode log and send the complete log, not only the lines containing OptGod.

Search the log for `OptGod REVIEW`. If the log contains init/setup lines but no `first PlayLayer::update reached`, that absence is itself important: it means the hook is not observing the expected update path.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build through GitHub Actions
