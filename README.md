# DashCore 0.7.6 — Android64 Bismuth adaptation diagnostics

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## What 0.7.6 measures

DashCore is still a diagnostic build. It does not replace Geometry Dash's renderer or hide objects.

After the first 180 measured frames of a level, it performs a same-frame comparison of:
- The scene-graph inspector's node and GameObject totals.
- The GameObject collection uploaded to the GLES compute probe.
- Three candidate interpretations of object bounds: parent-transformed, raw rectangle, and node-local content rectangle.
- Object geometry and state: triggers, effects, grouped/ungrouped objects, missing parents, self-hidden objects, hidden ancestors, invalid/zero/negative bounds, scaled objects, and rotated objects.
- The distance between the world-space center inferred from getBoundingRect() and the node's world-space anchor. This is a diagnostic comparison, not proof of the correct bounds.
- CPU time spent in scene scanning, GPU-input collection, bounds validation, and geometry inspection.
- Up to 12 generic object samples plus independent trigger and grouped-object samples, with class/group metadata, local and world anchors, raw rectangles, and transforms.
- Group-count range and per-object GPU-input reason flags for triggers, effects, grouped objects, grouped-near protection, missing parents, and invalid bounds.
- Counts of grouped objects classified near/far; parentless and invalid-bound records are forced visible in the diagnostic mask.
- Separate CPU dispatch-submission and barrier/readback/comparison timings.

The GLES 3.1+ compute probe checks whether the Android GPU can process the collected visibility mask and match the CPU reference. A PASS proves formula parity only; it does not prove that the chosen coordinates are correct or that culling is safe. Dispatch submission time is CPU submission time, not GPU execution time; readback can include synchronization stalls.

## Safety

- Live render culling is hard-disabled, even if the experimental setting is enabled.
- Effects and triggers are never hidden.
- No visibility flags, draw calls, transforms, updates, collisions, physics, or trigger execution are changed.
- The original Geometry Dash renderer remains in control.

## Test procedure

1. Install the Android64 0.7.6 artifact.
2. Leave the experimental render-culling setting disabled.
3. Open a normal level and play for at least 30 seconds after the level appears.
4. Prefer a trigger-heavy level containing color, move, pulse, rotate, and other triggers, plus grouped objects that move or rotate. Repeat with a second normal level if possible.
5. Send the complete Geode log, especially `TRIGGER-SAMPLE`, `GROUPED-SAMPLE`, `GPU-INPUT-CLASSIFICATION`, `OBJECT-GEOMETRY`, `BOUNDS`, `DIAGNOSTIC-COST`, and `BISMUTH-CULL` lines.
6. Report any visual issue, flicker, crash, or unusual stutter.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
