# DashCore — Bismuth Android Bridge

## 0.11.6-alpha — read-only Cocos compatibility bridge

This diagnostic prerelease adds only the Android64 adapter boundary needed to connect Bismuth's object pipeline to Geometry Dash's existing Cocos renderer. It does not install or activate a second renderer.

- Enumerates the existing level-object array and exposes the object index, ZLayer, normalized ZLayer, object draw order, spritesheet/parent mode, and base/detail blend flag.
- Unpacks each GameObject into glow, detail/color, base, black, and black-child sprite roles using Bismuth's expected traversal order: glow first; detail behind or in front according to the object's layering flag; and each sprite's negative-z children, sprite, then non-negative-z children.
- Exposes the existing Cocos batch nodes for each supported ZLayer, spritesheet, and blend combination. It reports their existing z-order and arrival order; it does not create, hide, remove, or reorder batch nodes.
- Sprite and object pointers are borrowed only during a visitor callback. The bridge copies the affine transform and classification values, does not retain engine objects, and never mutates Cocos state.
- Provides a read-only query of the EGL/GLES context that Cocos already made current. It never creates, destroys, switches, or replaces an EGL context and does not alter GL bindings.
- Keeps the original Cocos renderer as the only visible draw path; Geometry Dash vanilla rendering remains responsible for all visible output. There are no draw, visit, batch-submit, or present hooks in the Bismuth bridge. The bridge does not issue GL draw calls or change node visibility.
- The previous value-only renderer diagnostics remain separate and optional. The `capture-object-diagnostics` setting is **off by default**. With it off, DashCore does not scan `m_objects` or collect per-frame sprite snapshots.

The original Geode launcher EGL path is restored: request GLES 2, install the original EGLConfigChooser, then call Cocos `initView()`. The bridge reads whichever context is current; it does not request GLES 3.1. A GLES 2 context does not provide compute shaders, SSBOs, or indirect drawing, so those Bismuth GPU paths must remain disabled unless a future runtime query confirms support from the game's actual context.

This milestone is an integration foundation, not a Bismuth renderer port or an optimization release. It makes no FPS, frame-time, or visual-parity claim.

## Semiautomatic physical-device testing

The repeatable phone workflow, ZIP export instructions, JSONL analyzer usage, and OFF/ON comparison requirements are documented in [docs/PRUEBAS_SEMIAUTOMATICAS.md](docs/PRUEBAS_SEMIAUTOMATICAS.md). The standard-library analyzer is `scripts/analyze_launcher_diagnostics.py`; CI runs its unit tests before the Android64 build.

## Build and validation

The Android64 workflow checks the read-only bridge product contract, compiles the standalone renderer model and diagnostics tests with C++20, runs Cppcheck, and builds the Geode package for Android64 with Android API 29 minimum.

Cloud gameplay automation and physical Huawei Y9s validation are separate gates. A cloud emulator result is not evidence of performance on Kirin 710F / Mali-G51 hardware.

## Bismuth Android64 port boundary

Bismuth's desktop renderer requires APIs and shader behavior that are not guaranteed by the original Geode GLES 2 context. The bridge exposes the live capability profile and the Cocos object/sprite/batch metadata needed for a staged port. It does not emulate desktop OpenGL 4.3, compute shaders, SSBOs, SPIR-V, or multi-draw indirect on an unsupported context. See [docs/BISMUTH_ANDROID64_PORT.md](docs/BISMUTH_ANDROID64_PORT.md) and [docs/BISMUTH_BRIDGE.md](docs/BISMUTH_BRIDGE.md).

## Launcher Diagnostics bridge

On Android, the existing PlayLayer hooks send `onLevelStarted(null, null)` and `onLevelEnded()` lifecycle markers to `com.geode.launcher.LauncherDiagnostics` when that launcher component is present. The launcher remains responsible for recording frame cadence, Cocos callback wall time, GL-thread CPU time, process memory and thermal status. If the launcher class or method is unavailable, the bridge clears the lookup exception and leaves gameplay unchanged.

The bridge also forwards the completed `RendererDiagnosticSummary` as a structured JSON payload through `LauncherDiagnostics.onDashCoreLevelDiagnostics(String)`. Each level summary includes `capture_enabled`, `capture_skipped_frames`, index-build counters, and capture timings. This lets the exported JSONL identify its A/B variant without relying on the filename. These values are CPU-side diagnostics; they are not GPU draw-call counts, do not inspect/replace the visible draw path, and do not measure GPU execution time.
