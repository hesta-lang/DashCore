# DashCore — Android rendering optimization

## 0.10.11-alpha — render-sample progress and AutoTest completion visibility

- Adds sparse progress markers for the opt-in drawScene and postUpdate diagnostic sample.
- AutoTest checks both Geode's rolling log and Android logcat for the final sample marker, and reports the last known progress from each callback.
- Makes missing-process results safe to report instead of risking an uninitialized metric.
- No rendering, gameplay, physics, draw-order, or UI changes. This is diagnostic-only; no performance improvement is claimed.

## Development process

All fixes and optimization work follow the [DashCore Development and Validation Protocol](docs/DASHCORE-DEVELOPMENT-AND-VALIDATION.md). The protocol defines root-cause investigation, Android64 API verification, automated quality gates, gameplay-only AutoTest, and physical Huawei Y9s validation.

## 0.10.10-alpha — update timing histograms and gameplay-only AutoTest

- Adds mutually exclusive CPU-duration buckets for each measured drawScene and postUpdate frame, plus sampled Cocos renderer, base-game-layer update, and visibility callbacks.
- Reports ranges from 0–250 μs through over 8 ms while preserving average, maximum, and callback counts.
- Update/visibility hooks remain optional and runtime-discovered. Their timings can be nested and must not be added together; no unverified per-GameObject or section-management hook is introduced.
- Replaces the multi-variant cloud benchmark with one gameplay-and-diagnostics pass: culling OFF, Performance Diagnostics OFF, Render Pipeline Diagnostics ON, and no A/B comparison.
- AutoTest launches Geometry Dash, verifies the in-level HUD, supplies a simple tap pattern during the observation window, and saves Geode/logcat/frame-timing diagnostics. Cloud execution is not physical Huawei Y9s validation.
- No rendering, gameplay, physics, draw-order, or UI behavior changes. Diagnostics and experimental culling remain OFF by default; no FPS improvement is claimed.

## 0.10.9-alpha — update subphase timing

- Adds optional sampled timing for the active PlayLayer's inherited \`GJBaseGameLayer::update(float)\` callback and \`PlayLayer::updateVisibility(float)\`.
- These subphase hooks are optional and discovered at runtime. If unavailable, the core renderer profile remains usable and reports their unavailable status.
- Callback counts cover the profile window; one in every eight callbacks is timed. The callbacks can be nested, so their times are not additive and do not represent exclusive per-object cost.
- Original game callbacks are forwarded once; no gameplay/rendering changes are made. Diagnostics and experimental static culling remain OFF by default. No FPS improvement is claimed.


## 0.10.6-alpha — GameObject visual-hierarchy diagnostics

- Extends the opt-in render-pipeline profile with a one-shot read-only inventory of every usable entry in \`PlayLayer::m_objects\` after the 180-frame render sample.
- Traverses each GameObject's Cocos child subtree and records direct children, descendant nodes, descendant \`CCSprite\` instances, nested GameObjects, batch-node ancestors, and subtree depth.
- Reports five subtree-size buckets and up to eight objects with the largest child hierarchies to identify where sprite/node fan-out is concentrated.
- Applies per-object (4,096 nodes), per-object depth (64), and total (250,000 nodes) safety caps; reports truncation and scan duration.
- Per-root descendant counts can overlap when GameObjects are nested. A \`CCSpriteBatchNode\` ancestor is a hierarchy clue, not proof of actual atlas membership.
- The scan is read-only and runs once after the render sample. It does not alter node visibility, transforms, draw order, batching, gameplay, or UI.
- Performance Diagnostics and experimental static culling remain separately controlled; culling remains OFF by default.
- This remains diagnostic-only. No FPS improvement or physical-device optimization is claimed until the results are analyzed and compared on the Huawei Y9s.
