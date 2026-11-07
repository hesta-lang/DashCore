## 0.8.5-alpha — trigger ID sample selection correction
- Fixes the bounded trigger-ID sample so it records up to 24 objects for which the native `isTrigger()` predicate returns true, instead of sampling only the first 24 arbitrary GameObjects in scene traversal.
- Keeps the complete per-object-ID census, class-type census, and explicit zero-trigger warning; these remain observations of `isTrigger()` and are not an independent proof of trigger classification.
- Does not add speculative trigger-ID rules or call unverified classification methods.
- Keeps all diagnostics read-only and live render culling hard-disabled. No visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

## 0.8.5-alpha — object ID and trigger classification diagnostics
- Starts the diagnostic alpha series: all builds before the first validated optimization are explicitly prerelease alpha builds.
- Adds a census of GameObject `m_objectID` values and counts how many objects for each ID report `isTrigger() == true`.
- Adds up to 24 bounded object-ID samples with object ID, trigger flag, class type, group count, and Effect classification.
- Keeps class-type counts, trigger samples, bounds comparisons, 600-frame timing, and read-only GLES compute parity diagnostics.
- Uses numeric object IDs as evidence only; it does not guess trigger semantics or change object behavior.
- Keeps live render culling hard-disabled. No visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

## 0.8.3 — trigger and object-type diagnostics
- Adds a per-GameObject class-type census and counts how many objects of each class report `isTrigger() == true`.
- Increases the bounded trigger sample cap from 8 to 16, preserving position, parent, bounds, scale, and rotation details.
- Emits an explicit diagnostic when every sampled GameObject reports `isTrigger() == false`; this is not treated as proof that the level contains no triggers.
- Retains the 600-frame performance sample, grouped/effect protection, bounds comparisons, and read-only GLES compute parity probe.
- Keeps camera-space classification unvalidated and live render culling hard-disabled. No object visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

## 0.8.2 — extended runtime sampling
- Extends update and draw-scene timing samples from 180 to 600 frames (about 10 seconds at 60 FPS), providing a more representative window for intermittent stutters.
- Reduces progress-log frequency from every 30 frames to every 120 frames to keep diagnostic output smaller while retaining periodic measurements.
- Keeps the one-shot scene census, trigger detection (`GameObject::isTrigger()`), grouped-object samples, bounds comparisons, and GLES compute parity probe read-only.
- Clarifies that zero detected triggers means none were present in the sampled level; it is not evidence that trigger detection failed.
- Keeps live render culling hard-disabled. Camera transform/zoom is not yet validated; no object visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

## 0.8.1 — viewport-space validation
- Adds runtime reporting for window size, visible origin/size, and content scale factor to investigate the 0.8.0 bounds-space discrepancy.
- Explicitly records that camera translation/zoom is not yet applied to culling candidates; no candidate is considered safe for live culling.
- Updates runtime diagnostic labels to 0.8.1.
- Keeps triggers, effects, and grouped objects protected in the diagnostic mask and keeps live render culling hard-disabled.
- No object visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

# DashCore changelog

## 0.8.0 — Bismuth-inspired Android visibility classifier
- Replaces the GPU-input bounds source with each GameObject's local content rectangle transformed through Cocos' complete world-space node hierarchy.
- Treats missing parents, invalid/zero content dimensions, and non-finite transformed bounds as force-visible.
- Protects every trigger, Effect object, and grouped object in the GPU visibility mask; grouped objects remain protected until their trigger-driven transform behavior is implemented and validated.
- Keeps the GLES 3.1 compute visibility pass diagnostic-only; CPU/GPU parity does not certify coordinate correctness or authorize live culling.
- Removes repeated scene-graph traversals every 30 frames and reuses the final 180-frame snapshot for the collector comparison.
- Keeps live render culling hard-disabled. No object visibility, draw calls, transforms, updates, collisions, physics, or trigger execution are changed.

## 0.7.6 — trigger and grouped-object validation
- Adds independent trigger and grouped-object samples so generic traversal samples cannot hide these categories.
- Reports group-count range and dedicated sample totals.
- Adds per-object GPU-input reason flags for triggers, effects, grouped objects, grouped-near protection, missing parents, and invalid bounds.
- Forces parentless and invalid-bound records visible in the diagnostic mask; reports grouped-near/far classification totals.
- Corrects the GLES compute probe's remaining 0.7.3 labels to 0.7.6.
- Keeps live render culling hard-disabled; all new classification is read-only and diagnostic.


## 0.7.5 — final validation build
- Adds up to 12 concrete GameObject samples: class type, trigger flag, group count, parent presence, local/world anchor, raw rectangle, and object/parent transforms.
- Separates GLES compute dispatch submission time from barrier/readback/CPU comparison time; total probe time remains reported.
- Corrects diagnostic labels to 0.7.5 throughout the runtime probes.
- Keeps live render culling hard-disabled. No visibility, draw calls, transforms, updates, collisions, physics, or trigger execution are changed.
- This is a validation build, not an optimization release.

## 0.7.4
- Adds object-geometry diagnostics for triggers, effects, grouped/ungrouped objects, parentless nodes, self-hidden objects, and objects under hidden ancestors.
- Reports invalid, zero-sized, and negative bounding/content dimensions, plus scaled and rotated object counts.
- Compares the transformed center of getBoundingRect() with each node's world-space anchor as a diagnostic signal; it does not assume either coordinate interpretation is correct.
- Measures the CPU cost of scene scanning, GPU-input collection, bounds validation, and geometry inspection separately.
- Keeps the same-frame scene traversal cross-check and all three bounds candidates.
- Keeps the GLES compute and object visibility probes read-only; live render culling remains hard-disabled.

## 0.7.3
- Refreshes the scene-graph snapshot at the end of the 180-frame sample so object counts are compared at the same gameplay stage as the GPU collector.
- Adds read-only bounds diagnostics for three coordinate interpretations: parent-transformed bounding rectangle, raw bounding rectangle, and node-local content rectangle transformed to world space.
- Reports valid/invalid bounds, expanded-viewport visibility counts, and coordinate extents for each candidate.
- Resets the one-shot GPU object probe for each new PlayLayer so subsequent level tests are measured too.
- Clarifies that the GPU PASS proves CPU/GPU formula parity only, not correctness of the coordinate space or off-screen classification.
- Keeps live render culling hard-disabled; no visibility, triggers, collisions, updates, or draw calls are changed.

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
