# DashCore changelog

## 0.7.2
- Fixed diagnostic counter consistency: visited, protected, eligible, and grouped-object counts now use the same 30-frame sample.
- Delayed the GLES object visibility probe until the 180-frame level sample completes, allowing the scene graph to populate before collection.
- Added a direct comparison between the scene inspector and GPU collector using the same PlayLayer root.
- Hard-disabled live render culling for safety while object coverage and bounds are validated.
- Kept the GLES compute/SSBO test diagnostic-only; no Geometry Dash draw calls, visibility, triggers, collisions, or updates are changed.

## 0.7.1
- Added sampled GameObject visit diagnostics and clarified the engine initialization flag.
- Hardened GPU buffer readback validation.
- Updated the mod description and diagnostic settings text.
- Kept experimental live render culling disabled by default.

## 0.7.0
- Added an Android GLES 3.1+ compute-shader visibility-mask probe.
- Compares the GPU visibility result with a CPU reference using scene-object bounds.
- Preserves the original Geometry Dash renderer; the GPU pass is diagnostic only.
