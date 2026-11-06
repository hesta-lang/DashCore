# DashCore 0.6.0 — experimental Android64 render culling

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## Experimental render culling

Version 0.6.0 activates the optional render-culling path through the existing `CCNode::visit` hook.

When enabled, DashCore checks regular GameObjects against an expanded screen rectangle. Objects fully outside that rectangle are skipped for the current draw traversal. A generous 75% screen-size margin on every side is used to reduce visible pop-in as the camera/player moves.

### Always protected

- `GameObjectClassType::Effect` objects, including Geometry Dash trigger/effect objects.
- Any GameObject with `m_groupCount > 0`.
- Nodes that are not GameObjects.

The culling path does not change object positions, visibility flags, update scheduling, collision data, hitboxes, trigger execution, or physics. It skips only the object's Cocos `visit()` draw traversal. The option can be disabled in Geode settings; disable it if any object disappears incorrectly, flickers, or a level renders incorrectly.

**Experimental limitation:** this first implementation uses the object's bounding rectangle transformed through its parent into world space and compares it with the director window size. Geometry Dash camera behavior and unusual transformed objects may expose edge cases. The guard margin reduces risk but does not guarantee perfect culling in every custom level. Do not use this as a competitive/record-safe build until tested.

## Settings

- `Experimental render culling`: enabled by default in 0.6.0. Turn it off to restore the normal Cocos draw traversal.

## Diagnostics

The log reports:
- `culling-requested` and `culling-active` at level setup.
- `visited`: GameObjects encountered by the visit hook during the profiling sample.
- `eligible`: non-Effect, ungrouped GameObjects.
- `protectedEffect`: Effect-class objects excluded from culling.
- `protectedGrouped`: grouped GameObjects excluded from culling.
- `boundsChecks`: eligible objects checked against the expanded screen bounds.
- `culled`: eligible GameObjects whose draw traversal was skipped.
- Scene graph snapshots: total nodes, GameObjects, effects, grouped objects, hidden GameObjects, depth and scan duration.

Counters are collected during the first 180 draw samples after level setup. Culling itself remains active after the diagnostic sample finishes.

## Test procedure

1. Install the 0.6.0 Android64 artifact.
2. Open a normal level with Play, preferably the same level used for the 0.5.3 baseline.
3. Start from 0%; do not use editor preview or StartPos for the first comparison.
4. Play for 30–60 seconds, including sections with dense objects and moving camera.
5. Check that triggers, moving objects, portals, pads, orbs, and grouped structures still behave normally.
6. Send the complete Geode log. Include the `DashCore 0.6.0 CULL FINAL`, `SCENE-GRAPH FINAL`, and `dontCreateObjects` lines.
7. If anything disappears, flickers, or behaves differently, disable Experimental render culling and send the log.

For a fair comparison, use the same level, graphics settings, and device state. This is an experimental build; the counters and frame-time data are diagnostic and do not by themselves prove a performance improvement.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
