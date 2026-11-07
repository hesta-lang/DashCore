# DashCore 0.8.5-alpha — Android64 Bismuth viewport-validation prototype

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## What 0.8.5-alpha adds

Corrects the bounded trigger-ID sample selection: it now records up to 24 objects reporting `isTrigger() == true`, rather than the first 24 arbitrary GameObjects. Per-class and object-ID censuses remain unchanged. with transform and bounds details. This is a diagnostic alpha, not an optimization release. The 600-frame timing window and 120-frame progress cadence remain unchanged. Camera translation/zoom is not yet applied to candidate bounds, so no off-screen classification is considered valid for live rendering.

DashCore now has the first Android-side Bismuth-inspired visibility-classification stage. It is still diagnostic: it does not replace Geometry Dash's renderer and does not hide objects.

After the first 600 measured frames of a level, the adapter:
- Reuses the final scene-graph snapshot instead of repeatedly traversing the complete graph every 30 frames.
- Collects GameObjects from the PlayLayer tree.
- Builds each candidate rectangle from the GameObject's local content size and anchor, transformed by Cocos through the full node/ancestor hierarchy into world coordinates.
- Forces objects visible when their parent is missing, content dimensions are invalid/zero, or transformed coordinates are non-finite.
- Forces all triggers, Effect-class objects, and all grouped objects visible. Group transforms can be changed by triggers and are not yet mirrored by DashCore.
- Uploads the candidate records to a GLES 3.1+ compute shader and compares the returned mask with the CPU reference.
- Reports scene coverage, candidate-bounds alternatives, object categories, and CPU-side diagnostic costs.

## What a PASS means

A `BISMUTH-CULL ... CPU/GPU parity PASS` means only that the GLES compute shader returned the same result as the CPU formula for the supplied coordinates. It does **not** prove that the coordinates match the game's actual rendered geometry, that every renderable object was collected, or that culling is safe.

## Safety guarantees

- Live render culling is hard-disabled, even if the experimental setting is enabled.
- Every trigger, effect, and grouped object is forced visible in the diagnostic mask.
- Missing-parent and invalid-bounds records are forced visible.
- The original Geometry Dash renderer remains in control.
- No object visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

## Test procedure

1. Install the Android64 0.8.5-alpha artifact from the successful `Build OptGod (Android64)` workflow.
2. Leave `Experimental render culling (locked off)` disabled.
3. Open an ordinary level and play until the 600-frame sample completes (about 10 seconds at 60 FPS).
4. Use a level containing move, rotate, scale, alpha, toggle, color, and pulse triggers, plus grouped objects.
5. Repeat on a second level if possible.
6. Send the complete Geode log. Important lines: `SCENE-GRAPH FINAL-REFRESHED`, `GPU-INPUT-CLASSIFICATION`, `OBJECT-GEOMETRY`, `OBJECT-TYPE`, `OBJECT-ID`, `OBJECT-ID-SAMPLE`, `TRIGGER-DIAGNOSTIC`, `TRIGGER-SAMPLE`, `BOUNDS`, `DIAGNOSTIC-COST`, and `BISMUTH-CULL`.
7. Report any crash, flicker, missing object, late appearance, or unusual stutter.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
