# DashCore 0.7.0 — Android64 Bismuth adaptation prototype

Target: Geometry Dash 2.2081 / Geode 5.11.2 / Android64 (arm64-v8a), minimum Android SDK 29.

## 0.7.0: real-object GLES compute visibility test

This release adds the first Bismuth-inspired compute-culling prototype that receives real GameObject bounds collected from the active Geometry Dash scene graph.

The Android GLES 3.1 compute shader:
- Receives each GameObject's transformed bounding rectangle through an SSBO.
- Receives a protection flag for Effect objects, triggers, grouped objects near the player, and objects whose parent transform is unavailable.
- Computes a visibility mask using an expanded viewport with a 75% guard band.
- Reads the mask back and compares every result against a CPU reference implementation.
- Reports the number of processed objects, CPU/GPU visible counts, mismatches, dispatch workgroups, and elapsed time.

**Important:** this is a diagnostic GPU prototype only. It does not hide objects, change visibility flags, alter draw calls, update scheduling, collision, physics, or trigger execution. The original Geometry Dash renderer remains in control. The existing experimental CPU render-culling setting remains separate and opt-in.

## What the test proves

A successful result confirms that the device can compile and dispatch a compute shader over actual scene-object bounds and produce the same visibility decisions as the CPU reference. It does not yet prove a frame-rate improvement or that it is safe to replace Geometry Dash's renderer.

## Existing experimental CPU culling

The separate `Experimental render culling` option can skip the Cocos draw traversal for eligible objects outside the expanded screen rectangle. It is disabled by default. Effects and triggers are protected; grouped objects near the player are protected. Keep it disabled for ordinary play until it has been validated in dense levels.

## Test procedure

1. Install the Android64 0.7.0 artifact.
2. Leave `Experimental render culling` disabled for this test.
3. Open a normal level with Play and allow the first scene draw to complete.
4. Play for at least 30 seconds, preferably in a dense level.
5. Send the complete Geode log and include the `BISMUTH-CULL` lines.
6. Report any crash, visual issue, flicker, or abnormal behavior.

The GPU object probe runs once per game session, after the first measured draw. Its one-off cost is excluded from the frame-time sample.

## Build target

- Android64 / arm64-v8a
- Android minimum SDK 29
- Geometry Dash 2.2081
- Geode 5.11.2
- Release build via GitHub Actions
