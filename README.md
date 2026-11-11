# DashCore — Android rendering optimization

## 0.9.5-alpha — Bismuth-inspired conservative static-object culling

- Adds an opt-in CPU visibility pass for ordinary static objects outside an expanded camera viewport.
- Uses each object's local content bounds transformed through the full Cocos node hierarchy, with a 125% viewport guard band.
- Keeps triggers, effects, grouped objects, rotating objects, and objects with invalid bounds on the original Geometry Dash draw path.
- The feature is OFF by default and can be enabled through **Experimental static-object culling (alpha)**.
- This does not replace Cocos with Bismuth's custom renderer: Bismuth's current renderer requires desktop OpenGL 4.3 features that are not available as a direct Android GLES port.
- This is an experimental diagnostic/validation build; physical Huawei Y9s testing is required before any stable optimization claim.

## 0.9.4-alpha — remove inactive diagnostic hook overhead

- Makes the high-frequency hooks for `CCNode::visit`, `PlayLayer::postUpdate`, and `CCDirector::drawScene` opt-in at the Geode hook level.
- When **Performance Diagnostics** is OFF, these hooks are not auto-enabled, removing DashCore detour execution from the normal per-node/per-frame paths.
- When diagnostics are ON, the three hooks are enabled for the existing 600-frame sample and GLES/scene diagnostics.
- Leaves the original Cocos/Geometry Dash rendering, updates, physics, triggers, and draw order untouched. Experimental static-object culling is opt-in and OFF by default.
- This is an optimization candidate; no FPS improvement is claimed until the new binary passes cloud A/B and physical Huawei Y9s validation.

## 0.9.3-alpha — lower runtime overhead by default

- Disables optional timing instrumentation and diagnostic scene/GPU scans by default.
- Keeps the original Geometry Dash renderer and gameplay paths unchanged.
- Exposes **Performance Diagnostics** in Geode settings for opt-in profiling.
- Live render culling remains hard-disabled. The change reduces work added by DashCore; it is not yet a verified FPS uplift on the physical Huawei Y9s.

DashCore is an Android64 mod for Geometry Dash that investigates rendering performance and prepares a safer, Bismuth-inspired visibility optimization path. It measures frame and scene costs, inspects GameObjects and their bounds, and compares CPU visibility calculations with an OpenGL ES compute-shader implementation.

The long-term goal is to reduce unnecessary rendering work and stuttering on Android while keeping Geometry Dash's original gameplay, physics, controls, menus, and visual behavior intact.

**Current status:** DashCore 0.9.3-alpha is a conservative optimization review build. Performance Diagnostics are OFF by default, so normal play skips DashCore's 600-frame profiling window, scene-graph/bounds scans and one-shot GLES probes. Turn **Performance Diagnostics** ON only when collecting a diagnostic Geode log. The `General Enable` master switch remains available. Static-object culling is available as an opt-in alpha feature; keep it OFF for baseline play.

## Compatibility

- Geometry Dash 2.2081
- Geode 5.11.2
- Android64 (arm64-v8a)
- Minimum Android SDK 29

## What DashCore does

- Optionally samples frame timing and reports rendering/update performance indicators when **Performance Diagnostics** is enabled.
- Optionally inspects the PlayLayer scene graph and counts GameObjects by class and object ID.
- Optionally examines object bounds using local content dimensions and Cocos node transforms through the parent hierarchy.
- Treats objects with missing parents, invalid dimensions, or non-finite transformed coordinates conservatively.
- Identifies trigger candidates using native GameObject predicates and records bounded diagnostic samples.
- Keeps triggers, effects, and grouped objects protected in the diagnostic visibility mask because their behavior can depend on runtime changes.
- Optionally uses an OpenGL ES 3.1+ compute shader to compare GPU visibility calculations against a CPU reference.
- Reports diagnostic coverage and timing so the data can be reviewed before any optimization is enabled.

## What the diagnostics do — and do not — prove

A `BISMUTH-CULL ... CPU/GPU parity PASS` means only that the compute shader produced the same result as the CPU formula for the supplied coordinates. It does **not** prove that those coordinates match the game's actual rendered geometry, that every renderable object was collected, or that hiding objects would be safe.

Scene bounds are not yet fully validated against the game's camera translation and zoom. Therefore, off-screen classification is not considered reliable enough to control rendering.

## Safety

- Static-object culling is opt-in and OFF by default.
- The original Geometry Dash renderer remains in control.
- Diagnostic code does not change object visibility, draw calls, transforms, updates, collisions, physics, trigger execution, or level data.
- Trigger, effect, grouped, missing-parent, and invalid-bounds objects are handled conservatively in diagnostic classification.

## Validation procedure

1. Install the Android64 alpha build from the successful GitHub Actions workflow.
2. Keep **Experimental static-object culling (alpha)** disabled for the baseline; enable it only for a separate visual-validation run.
3. Turn **Performance Diagnostics** ON only for a diagnostic session, then open a normal level and play long enough for the frame-sampling window to complete (approximately 10 seconds at 60 FPS). Turn it OFF again for normal play.
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
- This is a cloud smoke test, not a substitute for physical-device playtesting on the Huawei STK-LX3 / Mali-G51. Static-object culling remains opt-in and OFF by default.

## Huawei Y9s reference profile

- The initial device profile is stored in `profiles/huawei-y9s.json`; its assumptions and limits are documented in `profiles/README.md`.
- It records the Huawei Y9s target (Kirin 710F, Mali-G51 MP4, 6 GB RAM, Android 10/API 29, 2340×1080/60 Hz) separately from settings that a cloud emulator can approximate.
- This is a **reference/calibration profile**, not a claim that the cloud emulator reproduces the phone's CPU, GPU, thermal behavior, or physical FPS. AutoTest's existing screenshot/tap coordinates are intentionally unchanged until an explicit workflow migration is validated.

## AutoTest integration — Huawei Y9s reference profile

- AutoTest now loads `profiles/huawei-y9s.json` before the baseline/with-DashCore A/B run and validates its schema.
- It records the actual cloud guest separately: Android build/API, model and board properties, ABI/native bridge, CPU information, guest RAM, `wm size`, `wm density`, reported refresh rate, and the SurfaceFlinger renderer when exposed.
- The reports include `device-profile.json`, `cloud-device-fingerprint.json`, and `device-profile-assessment.json`, plus the profile ID and `NOT_EMULATED` marker in each variant's `metrics.json`.
- The Markdown/JSON comparison report identifies the Y9s as the reference target and the Pixel cloud emulator as the execution environment. It shows API/ABI/resolution observations instead of claiming the emulator is a Y9s.
- The profile is descriptive and diagnostic only. AutoTest does not force the emulator's resolution, density, ABI, CPU, GPU or RAM to match the phone. Existing screenshots, tap coordinates, game navigation and frame-collection logic remain unchanged.
- A cloud benchmark can detect comparative regressions in the same emulator, but it is not a measurement of Kirin 710F / Mali-G51 performance. Physical Huawei logs remain necessary for device-specific validation.

## Kirin 710F CPU calibration for AutoTest

- AutoTest now includes a small Android ARM64 CPU probe that records the actual CPU topology and per-core integer throughput of its cloud guest.
- The separate **Kirin CPU Profile Tool** workflow builds a Termux-ready artifact, so the same probe can be run locally on the physical Huawei Y9s.
- It identifies Cortex-A73/A53 from ARM CPU part IDs when exposed; otherwise it reports frequency/capacity clusters only as candidates.
- No CPU throttle, clock override, or governor change is applied before a physical Y9s reference measurement is collected and reviewed.
- See [Kirin CPU calibration](docs/KIRIN-CPU-CALIBRATION.md). Cloud measurements remain cloud measurements and must not be presented as Kirin 710F results.

## Physical Huawei Y9s validation

The cloud A/B benchmark is not a physical-device performance result. Before claiming an optimization, run the controlled five-pair ON/OFF procedure on the Huawei Y9s using the same installed `.geode` package, the same level and route, and **Performance Diagnostics OFF** during timed runs. Toggle **General Enable** and fully restart Geometry Dash between runs. The procedure, result sheet, thermal controls, and separate diagnostic-log pass are documented in [Physical Huawei Y9s A/B validation](docs/PHYSICAL-HUAWEI-Y9S-VALIDATION.md).

Manual observation can identify repeatable stutters but does not provide exact FPS. Keep the build at `alpha` until physical-device evidence demonstrates a repeatable improvement without visual or gameplay regressions.

## Build target

DashCore is built for Android64 / arm64-v8a through GitHub Actions, targeting Geometry Dash 2.2081 and Geode 5.11.2.


AutoTest v3.9 conserva la validación del parser PNG y corrige la secuencia de toques: el segundo toque ahora pulsa Play en el menú principal (960,500), en lugar de volver a pulsar la posición del botón OK (960,793).


AutoTest v3.9 usa coordenadas nativas de las capturas 1920×1080: OK (960,793), Play del menú principal (960,500) y Play del nivel seleccionado (960,500).


## 0.8.7-alpha callback cadence diagnostics

- Separates callback execution cost (time spent inside `PlayLayer::postUpdate` and `CCDirector::drawScene`) from the wall-clock interval between consecutive callbacks.
- Reports callback-interval buckets, p50/p95/p99 intervals, and counts above 50 ms and 100 ms. Intervals in the 25–50 ms range are no longer automatically described as long stalls.
- The cloud v3.9 run reached active Stereo Madness gameplay and confirmed `setupHasCompleted`. Its 200 intervals above 25 ms are therefore treated as a cadence observation, not 200 proven rendering stalls; the single 385 ms maximum may include cloud-emulator scheduling or pauses.
- This is still diagnostic-only. It does not alter frame pacing, rendering, gameplay, or enable culling. Validate the new report on the physical Huawei/Mali-G51 before drawing device-performance conclusions.


## 0.8.9-alpha independent scene traversal diagnostics

- Decouples `CCNode::visit()` sampling from `CCDirector::drawScene`: the node-visit sample window is driven by 600 completed `PlayLayer::postUpdate` cycles, with samples every 120 update cycles.
- Reports total and maximum inclusive visit time, plus the GameObject subset, alongside the existing PlayLayer update and DrawScene callback timings.
- These visit timings are inclusive: parent visit durations contain child visit work, so totals overlap and must not be added together or treated as standalone renderer/GPU time.
- Sampling is limited to five update-aligned frames in the 600-update window to keep measurement overhead bounded. No extra per-frame scene traversal is introduced.
- Live culling remains hard-disabled. No object visibility, draw order, transforms, scheduling, gameplay, physics, triggers, controls, or UI are changed.
- Emits `NODE-VISIT-COST FINAL` independently of the DrawScene hook, with a fallback report from `postUpdate` if node visits stop. `DRAWSCENE-CPU` remains a separate optional metric. The purpose is to determine whether CPU scene traversal is a meaningful contributor before selecting any optimization.


## 0.9.0-beta — first conservative optimization

- Starts the optimization phase with a fast path in the `CCNode::visit` hook: when no PlayLayer diagnostic window is active and live culling is disabled, the hook immediately delegates to the original Cocos visit implementation.
- This avoids repeated profile-state checks for every visited node before gameplay profiling begins and after the diagnostic window ends.
- It does not skip nodes, change visibility, alter draw order, or modify object updates, triggers, collisions, physics, controls, menus, or level data.
- Live render culling remains hard-disabled. This is a deliberately small first optimization; its performance impact must be measured against the 0.8.9-alpha baseline on the cloud test and, ultimately, the physical Huawei/Mali-G51.


## 0.9.1-beta — controlled fast-path comparison

- Adds an internal startup override at the mod config path fast-path-mode.txt. The values on and off select the fast path; a missing or unrecognized file keeps the production default ON.
- AutoTest v4.2 runs the same cloud device, default level-entry sequence, and scripted jump stimulus twice using the same .geode package: first OFF, then ON.
- It confirms the selected mode from the mod log, waits for both 600-frame diagnostic windows to finish, and only then measures a separate 20-second process-CPU observation. Android gfxinfo framestats is collected as a best-effort frame-pacing signal when available.
- The SurfaceFlinger call used by AutoTest v4.1 has been removed; missing frame-stat data is reported as unavailable rather than failing the test.
- The A/B comparison does not enable or evaluate live culling. Live culling remains hard-disabled, and the cloud x86_64 + ARM-translation result is not a physical Huawei/Mali-G51 benchmark.


## 0.9.2-alpha — physical-device review and General Enable

- Adds the Geode setting **General Enable** (default ON).
- When OFF, DashCore remains installed and loaded by Geode, but its CCNode visit, PlayLayer, MenuLayer, and CCDirector hooks immediately delegate to the original game implementations. It also skips profiling, diagnostic probes, last-level recording, and automatic last-level startup loading.
- When ON, existing DashCore behavior is enabled, including the conservative CCNode fast path and the existing optional last-level startup setting.
- To compare on a physical Android device, keep the same installed .geode file and restart Geometry Dash after changing General Enable. Compare ON versus OFF using the same level, route, settings, and attempt conditions.
- This is an **alpha review build**, not a stable release. Live render culling remains hard-disabled; the setting does not enable experimental culling.
