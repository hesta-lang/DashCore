# DashCore — Android rendering optimization

DashCore is an Android64 mod for Geometry Dash that investigates rendering performance and prepares a safer, Bismuth-inspired visibility optimization path. It measures frame and scene costs, inspects GameObjects and their bounds, and compares CPU visibility calculations with an OpenGL ES compute-shader implementation.

The long-term goal is to reduce unnecessary rendering work and stuttering on Android while keeping Geometry Dash's original gameplay, physics, controls, menus, and visual behavior intact.

**Current status:** DashCore is in diagnostic/validation development. Its visibility analysis is experimental and read-only; live object culling is hard-disabled. It does not currently claim or deliver an active rendering-performance optimization.

## Compatibility

- Geometry Dash 2.2081
- Geode 5.11.2
- Android64 (arm64-v8a)
- Minimum Android SDK 29

## What DashCore does

- Samples frame timing and reports rendering/update performance indicators.
- Inspects the PlayLayer scene graph and counts GameObjects by class and object ID.
- Examines object bounds using local content dimensions and Cocos node transforms through the parent hierarchy.
- Treats objects with missing parents, invalid dimensions, or non-finite transformed coordinates conservatively.
- Identifies trigger candidates using native GameObject predicates and records bounded diagnostic samples.
- Keeps triggers, effects, and grouped objects protected in the diagnostic visibility mask because their behavior can depend on runtime changes.
- Uses an OpenGL ES 3.1+ compute shader to compare GPU visibility calculations against a CPU reference.
- Reports diagnostic coverage and timing so the data can be reviewed before any optimization is enabled.

## What the diagnostics do — and do not — prove

A `BISMUTH-CULL ... CPU/GPU parity PASS` means only that the compute shader produced the same result as the CPU formula for the supplied coordinates. It does **not** prove that those coordinates match the game's actual rendered geometry, that every renderable object was collected, or that hiding objects would be safe.

Scene bounds are not yet fully validated against the game's camera translation and zoom. Therefore, off-screen classification is not considered reliable enough to control rendering.

## Safety

- Live render culling is hard-disabled.
- The original Geometry Dash renderer remains in control.
- Diagnostic code does not change object visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data.
- Trigger, effect, grouped, missing-parent, and invalid-bounds objects are handled conservatively in diagnostic classification.

## Validation procedure

1. Install the Android64 diagnostic build from the successful GitHub Actions workflow.
2. Keep any experimental render-culling option disabled.
3. Open a normal level and play long enough for the frame-sampling window to complete (approximately 10 seconds at 60 FPS).
4. Where possible, test a level containing move, rotate, scale, alpha, toggle, color, and pulse triggers, as well as grouped objects.
5. Repeat on another level and provide the complete Geode log.
6. Report crashes, flicker, missing objects, late appearances, or unusual stuttering.

Useful log entries include `SCENE-GRAPH`, `GPU-INPUT-CLASSIFICATION`, `OBJECT-GEOMETRY`, `OBJECT-TYPE`, `OBJECT-ID`, `TRIGGER-ID-SAMPLE`, `TRIGGER-DIAGNOSTIC`, `TRIGGER-SAMPLE`, `BOUNDS`, `DIAGNOSTIC-COST`, and `BISMUTH-CULL`.

## 0.8.6-alpha validation change

- Defers the one-time GLES compute/SSBO probe until after the 600-frame gameplay timing window and its object diagnostics. This prevents the probe's CPU/GPU work from inflating a measured `DrawScene` duration.
- The probe remains read-only and still requires an active GL context. Its result is diagnostic only; it does not enable object culling.
- Live culling remains hard-disabled, and no gameplay, rendering, or UI behavior is intentionally changed.

## AutoTest v3.9 — gameplay smoke test

- Installs the authorized Android Geometry Dash APK and the Geode launcher in an ARM-translated cloud emulator.
- Stages DashCore in the launcher's actual Geode mods directory and confirms the mod appears in Geode's log.
- Captures the first-run Terms notice, uses the captured landscape screenshot coordinates directly for `adb shell input tap` (Android's `wm size` reports the panel's portrait natural orientation even while the game is rotated), dismisses the notice at (960,793), taps the main-menu Play button at (960,500), and then taps the selected level's Play control at (960,500). It retains notice, main-menu, level-select, and final screenshots.
- A runtime PASS requires DashCore's `PlayLayer::setupHasCompleted` hook to appear in the collected Geode log. If the game remains open but no gameplay hook appears, the result is INCONCLUSIVE rather than a false PASS. Tap coordinates are validated against the captured PNG dimensions and sent in the current rotated logical coordinate space; `adb shell wm size` is recorded for diagnostics, not used to scale landscape taps.
- This is a cloud smoke test, not a substitute for physical-device playtesting on the Huawei STK-LX3 / Mali-G51. Live culling remains hard-disabled.

## Build target

DashCore is built for Android64 / arm64-v8a through GitHub Actions, targeting Geometry Dash 2.2081 and Geode 5.11.2.


AutoTest v3.9 conserva la validación del parser PNG y corrige la secuencia de toques: el segundo toque ahora pulsa Play en el menú principal (960,500), en lugar de volver a pulsar la posición del botón OK (960,793).


AutoTest v3.9 usa coordenadas nativas de las capturas 1920×1080: OK (960,793), Play del menú principal (960,500) y Play del nivel seleccionado (960,500).
