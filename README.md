# DashCore — Geode Native Renderer

## 0.11.8-alpha — restore the original Geode rendering path

This validation release removes the experimental Bismuth/OpenGL ES bridge and returns rendering to Geometry Dash's original Cocos renderer supplied through Geode.

- Removes the Bismuth sprite/object bridge and the CPU trigger-state mirror.
- Removes the custom GLES backend that created OpenGL buffers and uploaded prepared geometry.
- Removes GLES capability probing, Android Bismuth bridge helpers, and the corresponding tests.
- Removes explicit GLESv3/EGL link dependencies from DashCore.
- Keeps only CPU-side level lifecycle and timing diagnostics. The capture-object-diagnostics setting remains off by default.
- The original PlayLayer::postUpdate method is called first; DashCore measures its CPU duration afterward. It does not hook Cocos draw/visit/present functions.
- No trigger functions are replaced or mirrored. No GameObject, Cocos node, sprite, batch, Z-order, visibility, physics, or gameplay state is changed.

There is no custom OpenGL ES renderer, no GPU buffer upload, no Compute Shader, and no Bismuth draw submission in this version. The only visible renderer is the original Geometry Dash/Geode Cocos renderer.

## Diagnostics

The optional capture-object-diagnostics mode is a CPU-side read-only inspection path for A/B diagnosis. It is disabled by default and should remain off during normal play. With it disabled, DashCore does not traverse the level object tree or capture sprites; it only observes the existing PlayLayer lifecycle and records CPU callback timing.

Launcher Diagnostics can continue recording callback cadence, wall time, thread CPU time, process memory and thermal state. GPU timer queries, GL capability probing and EGL swap-interval overrides are removed from the launcher diagnostic path.

## Build and validation

The Android64 workflow tests the CPU geometry/diagnostic models and product contract, runs Cppcheck, and builds the Geode package for Android64 with Android API 29 minimum.

This is a rollback/validation prerelease, not an optimization release. It makes no FPS, rendering-parity, or performance-improvement claim.
