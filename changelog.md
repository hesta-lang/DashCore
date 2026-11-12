## 0.10.2-alpha — Android chrono portability fix
- Replaces the `std::chrono::microseconds` alias at both geometry timing sites with an explicit microsecond duration type, avoiding the Android64 Clang failure reported by build #35.
- Keeps the diagnostic scheduling change from 0.10.1-alpha; scene inspection remains read-only and experimental culling remains OFF by default.
- Validation build only. No performance improvement is claimed; physical Huawei Y9s validation remains required.

## 0.10.1-alpha — Android diagnostic scheduling
- Moves the final read-only scene-geometry scan from the `CCDirector::drawScene` callback to `PlayLayer::postUpdate` after 600 confirmed gameplay updates.
- The Android AutoTest confirmed that `PlayLayer::postUpdate` ran 600 updates while the hooked `CCDirector::drawScene` callback did not reach its 600-frame completion path. This caused the previous diagnostic timeout.
- Keeps drawScene profiling optional; only CCNode::visit and PlayLayer::postUpdate are required for the geometry pass.
- AutoTest waits for DIAGNOSTIC-COST instead of waiting for a candidate line, and still requires the candidate / parent / bounds markers in the completed report.
- The isolated pass keeps culling OFF; the normal FPS A/B measurements remain free of the expensive geometry scan.
- No optimization claim is made. Cloud testing is diagnostic only; physical Huawei Y9s validation remains required.

## 0.10.0-alpha — isolated geometry diagnosis
- Adds a separate AutoTest launch with Performance Diagnostics ON and static culling explicitly OFF; the FPS A/B runs keep diagnostics OFF.
- Adds the diagnostics-mode.txt AutoTest override; without the file, the normal Geode Performance Diagnostics setting remains authoritative.
- Requires CCNode::visit, PlayLayer::postUpdate and CCDirector::drawScene hooks to all enable before a full diagnostic pass is accepted.
- Identifies the nearest eligible GameObject to the existing guard-band edge and records its object ID, class type, raw bounding rectangle, content size, anchor, position, transformed world bounds, viewport margins and up to eight ancestor transforms.
- Adds an isolated diagnostic report and strict marker validation to AutoTest. The run is rejected if candidate, hierarchy or scene-geometry records are missing.
- Does not change culling eligibility, the 125% guard band, rendering behavior or the default OFF setting. Cloud results remain diagnostic only; physical Huawei Y9s validation is still required.

## 0.9.9-alpha — GameObject bounds fallback
- AutoTest 214 showed that all 122 eligible Stereo Madness GameObjects had invalid/empty CCNode content sizes, so the previous culling bounds path could not classify any candidate.
- When content size is unusable, tries GameObject::getBoundingRect() and transforms its four parent-relative corners through the parent node into world space.
- Keeps fail-open behavior: missing parent, invalid/zero bounding rectangle, invalid transformed bounds or viewport are never culled.
- Adds contentSizeFallbacks and invalidBoundingRect telemetry; AutoTest validates and reconciles the expanded reason report.
- Preserves the 125% guard band, object eligibility protections and OFF-by-default setting. This is still diagnostic/experimental, not a validated optimization.
- Cloud emulator results are not physical Huawei Y9s validation.

## 0.9.8-alpha — categorized culling rejection diagnostics
- Adds mutually exclusive per-GameObject telemetry for protected effects, triggers, grouped objects and rotating objects, plus missing director, invalid content size, invalid anchor, invalid world bounds and invalid viewport outcomes.
- Splits safe eligible candidates into within-guard-band, offscreen-X, offscreen-Y and offscreen-both categories, with counts reconciled against the existing visited/eligible/culled totals.
- Records the eligible GameObject closest to crossing the existing 125% guard band, including object ID, world-space bounds and clearance.
- Extends AutoTest to parse, reconcile and report all rejection categories; missing or inconsistent diagnostic output now makes culling validation INCOMPLETE.
- Does not change the culling threshold, eligibility rules, renderer path or default setting. Experimental culling remains OFF by default.
- Cloud testing remains diagnostic only; physical Huawei Y9s visual and performance validation is required before any optimization claim.

## 0.9.7-alpha — delayed culling telemetry window
- Waits for 600 completed gameplay updates after level setup before starting culling telemetry, avoiding the initial level-construction and camera-settling period.
- Collects visited, eligible, and actually skipped GameObject counts only during the following 120 gameplay-frame visits; the sample reports its warmup and measurement windows separately.
- Updates AutoTest parsing and validation so a culling run is accepted only when the full 600-frame warmup and 120-frame sample are confirmed.
- Keeps the experimental culling setting OFF by default and does not change the culling eligibility rules or Geometry Dash's normal rendering path.
- Cloud measurements remain diagnostic only; physical Huawei Y9s visual and performance validation is still required.

## 0.9.6-alpha — hook registration correction and culling A/B validation
- Resolves optional hooks from Geode's actual registered-hook list using method display-name suffixes, avoiding hard-coded map-key assumptions for namespaced Cocos classes.
- Explicitly keeps diagnostic hooks auto-disabled until Performance Diagnostics or the experimental culling feature requires them.
- Adds an AutoTest-only `static-culling-mode.txt` override for repeatable culling-OFF/ON runs; the normal Geode setting remains authoritative when the file is absent.
- Adds bounded runtime telemetry for visited, eligible, and skipped GameObjects without enabling the 600-frame diagnostic suite; per-object counters stop after the first 120 update frames.
- Expands cloud AutoTest to compare no mod, DashCore with culling OFF, and DashCore with culling ON.
- The setting remains OFF by default. Cloud results do not validate Huawei Y9s rendering/performance; physical testing is still required.

## 0.9.5-alpha — conservative Bismuth-inspired static-object culling
- Adds an opt-in CPU frustum-culling path inspired by Bismuth's object visibility stage, adapted to Geometry Dash's existing Cocos renderer and Android GLES environment.
- Computes bounds from the GameObject's local content rectangle through its complete Cocos world transform and compares them with the visible viewport plus a 125% guard band.
- Culls only ordinary, ungrouped, non-trigger, non-effect, non-rotating objects. Grouped objects remain protected because trigger-driven transforms are not mirrored by this adapter.
- Missing/invalid dimensions, non-finite transforms, and invalid viewport data always fall back to vanilla drawing.
- The option is OFF by default. The original Geometry Dash renderer, draw order, gameplay, physics, trigger execution, and menus remain in control.
- This is an experimental alpha feature, not a validated optimization release. Cloud tests do not replace physical Huawei Y9s / Mali-G51 visual and performance validation.

## 0.9.4-alpha — remove inactive high-frequency diagnostic hooks
- Makes the `CCNode::visit`, `PlayLayer::postUpdate`, and `CCDirector::drawScene` hooks opt-in at the Geode hook level.
- When **Performance Diagnostics** is OFF, these hooks are not auto-enabled, removing DashCore's detour from the per-node and per-frame execution paths during normal play.
- When diagnostics are ON, DashCore explicitly enables the three hooks for the existing 600-frame profile and read-only scene/GLES probes.
- Keeps the original Geometry Dash renderer, object updates, draw order, physics, triggers, controls, and level data unchanged. Live render culling remains hard-disabled.
- This is an alpha optimization candidate; FPS gains are not claimed until cloud A/B and physical Huawei Y9s validation complete.

## 0.9.3-alpha — reduce diagnostic overhead during normal play
- Makes performance diagnostics opt-in and OFF by default. Normal gameplay no longer runs the 600-frame callback timers, sampled CCNode visit timing, full scene-graph scans, object-bounds inspections, or one-shot GLES diagnostic probes.
- Preserves the original Geometry Dash update and rendering paths. The diagnostics can be enabled from Geode's **Performance Diagnostics** setting when a Geode log is needed.
- Keeps **General Enable**, last-played-level behavior, and the conservative CCNode fast path. Live object culling remains hard-disabled; no objects, triggers, physics, controls, UI, or level data are altered.
- This is a conservative alpha optimization intended to remove DashCore's own diagnostic work, not a claim that the game's base renderer or the Huawei Y9s has already gained a measured FPS increase.

## 0.8.5-alpha — trigger ID sample selection correction
- Fixes the bounded trigger-ID sample so it records up to 24 objects for which the native `isTrigger()` predicate returns true, instead of sampling only the first 24 arbitrary GameObjects in scene traversal.
- Keeps the complete per-object-ID census, class-type census, and explicit zero-trigger warning; these remain observations of `isTrigger()` and are not an independent proof of trigger classification.
- Does not add speculative trigger-ID rules or call unverified classification methods.
- Keeps all diagnostics read-only and live render culling hard-disabled. No visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data are modified.

## 0.8.4-alpha — object ID and trigger classification diagnostics
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
