# Experimental Progressive Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish an isolated, reversible Android64 renderer pipeline in DashCore, initially observational and later capable of replacing only validated level-object categories.

**Architecture:** Keep the original Geometry Dash renderer authoritative while a separate collector builds safe render snapshots and a planner models sprite order/batches. Do not activate experimental drawing until the parallel pipeline, ordering model, and category parity checks have passed; preserve vanilla fallback for every unsupported case.

**Tech Stack:** C++20, Geode SDK 5.11.2, Geometry Dash 2.2081 bindings, Android NDK/arm64-v8a, GLES capability checks, repository CI and Python contract tests.

**Spec:** `docs/superpowers/specs/2026-11-12-experimental-renderer-design.md`

## Global Constraints
- Target Geometry Dash 2.2081, Geode 5.11.2, Android64 (arm64-v8a), Android 10/API 29, Huawei Kirin 710F/Mali-G51 MP4.
- Only level-object rendering is in scope; global shaders/effects, UI, editor, HUD, player rendering, physics, triggers and gameplay remain vanilla.
- Experimental drawing stays disabled until parity gates pass; vanilla fallback remains available.
- Do not assume GLES 3.1/3.2, compute shaders, SSBOs or indirect drawing; detect runtime capabilities.
- Use verified Geode bindings; no guessed offsets or unsafe hooks.
- Do not copy Bismuth source; implement independently from public concepts and verified bindings.
- Do not ship profiling, diagnostics or AutoTest as product functionality; CI and tests remain development tools.
- Diagnostic/validation releases use prerelease label alpha; no optimization claim without repeatable physical-device evidence.

## Review Focus
- Destroyed/reloaded objects: no dangling references; test reset, exit and reload cleanup.
- Unknown sprite/atlas/blending: exclude and preserve vanilla; test unsupported descriptor fallback.
- Z-order/child order: batching cannot reorder; test stable order across batch boundaries.
- Dynamic trigger/group state: refresh transforms, colors, opacity and visibility; test changing state.
- Backend capability mismatch: safe backend or vanilla path; test capability selection without emulator parity assumptions.

---

### Task 1: Isolate experimental product surface and establish build contract

**Files:** Modify `mod.json`, `CMakeLists.txt`, `README.md`; inspect diagnostic-only source and settings; add/update contract tests.

**Interfaces:** Product surface contains no diagnostic/profiling/AutoTest runtime features; Android64 build remains supported.

- [ ] Inventory current product files and diagnostic registrations; distinguish diagnostic-only code from compatibility infrastructure.
- [ ] Add a contract test asserting no diagnostic/profiling/AutoTest settings or product features are exposed.
- [ ] Run the test and verify it fails against baseline.
- [ ] Remove diagnostic-only product registrations and update metadata/docs, preserving required Geode compatibility.
- [ ] Run contract tests and Android64 build; expected: pass, experimental drawing still inactive.
- [ ] Commit: `chore: isolate experimental renderer product`.

### Task 2: Define safe renderer data contracts

**Files:** Create `src/renderer/RenderTypes.hpp`, `RenderSnapshot.hpp`, `RenderSnapshot.cpp`; add contract tests.

**Interfaces:** `enum class RendererMode { ObserveOnly, CompareOnly, ExperimentalDraw };`; `SpriteDescriptor` holds validated resource identity, UV/geometry, transform, color, opacity, blend, layer/order and compatibility state; `RenderSnapshot` owns descriptor storage and frame/level identity, never live game-object pointers; `RenderSnapshot::clear()`.

- [ ] Test empty snapshot, deterministic descriptor order, invalid descriptor classification and reset.
- [ ] Verify test failures before implementation.
- [ ] Implement owned snapshot/types with stable IDs and no persistent raw game pointers.
- [ ] Run tests; expected: snapshot contracts pass.
- [ ] Commit: `feat: define experimental render snapshot contracts`.

### Task 3: Implement lifecycle-safe observe-only collection

**Files:** Create `src/renderer/LevelObjectCollector.hpp/.cpp`; modify entry/lifecycle file only for init/clear; add lifecycle tests.

**Interfaces:** `bool LevelObjectCollector::collect(verified_level_context, RenderSnapshot& out)`; `void LevelObjectCollector::reset()`. Exact context type must be determined from verified GD 2.2081 bindings.

- [ ] Test repeated collect/reset, level transition, missing context and destroyed objects.
- [ ] Verify failures before implementation.
- [ ] Inspect binding/member access paths; document unavailable fields rather than guessing.
- [ ] Implement read-only collection; no Cocos node mutation or draw calls.
- [ ] Run tests and Android64 build; expected: safe lifecycle, no draw interception.
- [ ] Commit: `feat: collect level render data without drawing`.

### Task 4: Model layer order and compatible batches

**Files:** Create `src/renderer/LayerOrderModel.hpp/.cpp`, `BatchPlanner.hpp/.cpp`; add planner tests.

**Interfaces:** deterministic ordered descriptors and batches keyed by layer, texture/atlas and blend state; stable order within layers.

- [ ] Test layer ordering, stable same-layer order, compatible batching and separation of differing texture/blend state.
- [ ] Verify failures before implementation.
- [ ] Implement order model and planner without sorting across visual-order boundaries.
- [ ] Run tests; expected: all ordering/batch contracts pass.
- [ ] Commit: `feat: model sprite order and compatible batches`.

### Task 5: Add capability-gated GLES backend without presenting output

**Files:** Create `src/renderer/RenderBackend.hpp`, `GLESBackend.hpp/.cpp`; add capability tests.

**Interfaces:** backend initialization result and capability record; CPU/GLES conventional path may be selected if supported; unsupported features rejected safely. No compute/SSBO/indirect-draw requirement.

- [ ] Test capability selection for supported and unsupported feature combinations.
- [ ] Verify failures before implementation.
- [ ] Implement runtime GLES context/extension detection and backend selection.
- [ ] Run tests and Android64 build; expected: no unsupported assumption and no experimental draw.
- [ ] Commit: `feat: add capability-gated render backend`.

### Task 6: Build parallel geometry output and parity checks

**Files:** Create `src/renderer/GeometryBuilder.hpp/.cpp`; add geometry/parity tests; update validation docs.

**Interfaces:** validated snapshots and batches produce CPU-side vertex/index output plus completeness result, without replacing framebuffer.

- [ ] Test UV mapping, transforms, color/opacity propagation, batch boundaries and unsupported-category exclusion.
- [ ] Verify failures before implementation.
- [ ] Implement deterministic CPU-side geometry generation from verified descriptor values.
- [ ] Run tests; expected: geometry deterministic and fallback classifications correct.
- [ ] Document physical-device comparison protocol for vanilla/experimental structures and screenshots.
- [ ] Commit: `feat: prepare parallel renderer geometry`.

### Task 7: Enable one narrow category only after parity approval

**Files:** Modify renderer mode/controller and verified draw integration point; add exclusive ownership/fallback tests; update changelog and validation notes.

**Interfaces:** one parity-approved category can opt into experimental drawing; all others remain vanilla.

- [ ] Test exactly-one-renderer ownership and vanilla fallback for unsupported objects.
- [ ] Verify failures before integration.
- [ ] Implement smallest safe category switch, disabled by default, with reliable vanilla fallback.
- [ ] Run tests/build and repeated physical-device validation; expected: no missing/duplicate sprites, flicker, ordering/gameplay changes or crashes.
- [ ] Commit: `feat: add opt-in experimental object category`.

### Task 8: Gate expansion and performance optimization

**Files:** Modify renderer category registry/backend only as evidence permits; update physical validation protocol; extend parity/fallback tests.

- [ ] Add categories one at a time after their own visual/functional tests pass; unsupported dynamic objects, triggers, groups and effects remain vanilla.
- [ ] Validate base/detail/glow/black layers, atlas changes, blending, dynamic colors and group transforms independently.
- [ ] Only after equivalence, evaluate buffer reuse, state reduction, conservative culling/chunking or optional compute shaders.
- [ ] Run Android64 build, automated checks and repeated Huawei Y9s measurements; report only measured results.
- [ ] Commit each independently validated category/optimization with a scoped message.

## Execution notes
- Never jump from parallel collection directly to active drawing.
- Keep commits small and independently reviewable.
- AutoTest/emulator validates only exercised checks; it does not establish parity or performance on the physical Mali-G51.
- The separately requested DashCore optimization alpha is a subsequent distinct task based on current DashCore documentation; do not conflate it with this renderer branch or claim validation before device testing.
