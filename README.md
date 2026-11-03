# OptGod

Experimental, vanilla-preserving rendering optimization for Geometry Dash.

## Target

- Geometry Dash: 2.2081 (Android)
- Geode: 5.11.2
- ABI: Android64 / arm64-v8a
- Android minimum SDK: 29
- Build: GitHub Actions

## Design rules

OptGod is not a low-detail or visual-removal mod. It must preserve the original menus, textures, colors, particles, shaders, object behavior, physics, triggers, collisions, and hitboxes.

The first optimization stage will investigate camera-frustum culling: skip render submission for objects that are provably outside the active gameplay camera, without disabling their game logic or changing their visibility state globally. Objects with uncertain bounds, dynamic effects, triggers, or special render behavior must fail open and continue rendering.

Later stages may investigate batching and reducing redundant per-frame render preparation. GPU-side culling is a separate, more invasive research stage.

## Safety and validation

The initial build is intentionally passive. No render hooks are enabled until the target Geometry Dash/Geode Android bindings are verified. Each optimization must be tested with moving objects, particles, glow, shaders, portals, triggers, editor mode, and camera transitions. Correctness takes priority over benchmark results.

## Build

Pushes and manual runs build an Android64 .geode package through the official geode-sdk/build-geode-mod GitHub Action.
