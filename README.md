# DashCore — Bismuth Android Bridge

## 0.11.7-alpha — Bismuth trigger-state adapter and A/B diagnostics

This branch preserves the vanilla Geometry Dash renderer and adds an optional CPU-side Bismuth-compatible trigger-state mirror for Android64. The mirror is disabled by default and does not change gameplay or visible rendering.

- Adds a Bismuth-style group-combination model: unique sets of up to ten group IDs share one transform/opacity state, with CPU move/rotation updates, toggle state, pulse offsets and checkpoint snapshots.
- Observes the native Move, Rotate, Follow and Toggle trigger state while still calling every original Geometry Dash method. It samples Alpha opacity, Color channel state and Pulse action metadata after vanilla postUpdate.
- Exports a fixed 64-byte aligned group-state record layout for a future GPU backend. This is a data contract only; the current milestone does not upload a GPU buffer or dispatch a Compute Shader.
- Keeps the original Cocos renderer as the only visible draw path. No object positions, rotations, visibility, physics, collisions, sections, Z-order, batching, Cocos nodes or draw calls are changed by the adapter.
- The existing Bismuth sprite bridge still exposes object/sprite roles, traversal order, affine transforms and existing batch-layer metadata with borrowed pointers valid only during visitor callbacks.
- The current launcher uses Geode's original GLES 2 context. Compute Shaders and SSBOs are unavailable in that active context; the bridge never creates another EGL context or changes the requested context version.
- Keeps capture-object-diagnostics off by default. The new enable-bismuth-trigger-state-adapter setting is also off by default and enables only the CPU shadow-state mirror.
- Retains existing object-capture diagnostics, launcher JSONL markers, A/B measurements and conservative sprite extraction.

The adapter is a validation milestone, not an optimization release. It does not yet draw through Bismuth, does not claim pixel parity, and makes no FPS/performance claim. See docs/BISMUTH-TRIGGER-ADAPTER.md, docs/BISMUTH_BRIDGE.md, and docs/BISMUTH_ANDROID64_PORT.md.

## Semiautomatic physical-device testing

The repeatable phone workflow, ZIP export instructions, JSONL analyzer usage, and OFF/ON comparison requirements are documented in docs/PRUEBAS_SEMIAUTOMATICAS.md. The standard-library analyzer is scripts/analyze_launcher_diagnostics.py; CI runs its unit tests before the Android64 build.

## Build and validation

The Android64 workflow checks the product contract, compiles the standalone renderer and trigger-state model tests with C++20, runs Cppcheck, and builds the Geode package for Android64 with Android API 29 minimum.

Cloud gameplay automation and physical Huawei Y9s validation are separate gates. A cloud emulator result is not evidence of performance on Kirin 710F / Mali-G51 hardware.

## Launcher Diagnostics bridge

On Android, the experimental PlayLayer hooks send onLevelStarted(null, null) and onLevelEnded() lifecycle markers to com.geode.launcher.LauncherDiagnostics when that launcher component is present. The launcher remains responsible for recording frame cadence, Cocos callback wall time, GL-thread CPU time, process memory and thermal status. If the launcher class or method is unavailable, the bridge clears the lookup exception and leaves gameplay unchanged.

The bridge also forwards the completed RendererDiagnosticSummary as a structured JSON payload through LauncherDiagnostics.onDashCoreLevelDiagnostics(String). These values are CPU-side diagnostics, not GPU draw-call counts, and do not inspect or replace the visible draw path.
