# Parallel Object Renderer Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first safe DashCore experimental-renderer milestone: a read-only parallel pipeline that captures level-object sprites, preserves source ordering, prepares compatible batches and geometry, and never replaces or issues game draw calls.

**Architecture:** Keep Geode-facing collection separate from pure C++20 data, extraction, ordering, batching, and geometry modules. The Android GLES backend may detect capabilities and prepare resources, but this milestone stays in observe/compare mode; all visible rendering remains vanilla. Unsupported or incomplete data is excluded.

**Tech Stack:** C++20, Geode SDK 5.11.2, Geometry Dash 2.2081 bindings, Android64 (`arm64-v8a`), Android API 29 minimum, OpenGL ES capability queries, GitHub Actions, Python 3 standard-library tests, and a host C++ compiler for pure-model tests.

**Spec:** `docs/superpowers/specs/2026-11-12-experimental-renderer-design.md`

## Global Constraints

- Target Geometry Dash 2.2081, Geode 5.11.2, Android64 (`arm64-v8a`), Android 10 / API 29 minimum.
- Physical reference device: Huawei Y9s, Kirin 710F, Mali-G51 MP4.
- Work only on `experimental-renderer`; do not modify or merge into `main`.
- Product scope is renderer compatibility and level-object rendering only.
- Global shaders/effects, backgrounds, particles outside the validated scope, UI, menus, HUD, editor, and player rendering remain vanilla.
- Do not change physics, collisions, triggers, group logic, gameplay timing, input, or object behavior.
- No profiling, performance-diagnostics, culling, or AutoTest feature may ship in the mod. Build tests, static checks, and cloud gameplay automation remain development/CI tooling only.
- No draw interception or experimental draw call is allowed in this milestone.
- Keep vanilla drawing as the only visible output; never double-draw an object.
- Preserve source draw order; never reorder across layers or incompatible blending states to improve batching.
- Unknown resources, unsupported blending, incomplete sprite data, and invalid transforms stay outside the experimental candidate set.
- Use only verified Geode bindings for the pinned Android64 target; do not guess offsets or hook unsupported/inline methods.
- Detect GLES version/extensions at runtime. Compute shaders, SSBOs, and indirect drawing are optional future capabilities, never startup requirements.
- Do not copy or link Bismuth source code; all implementation code and data structures must be original.
- Every experimental or validation package remains prerelease `alpha`; no optimization claim is allowed without repeatable physical-device evidence.

## Review Focus

- **Multiple sprites under one GameObject or nested batch nodes:** enumerate each supported sprite once; never assume one GameObject equals one sprite. Add fixtures in Tasks 3–4.
- **Child reordering and z-order changes:** preserve observed vanilla order, including equal-z traversal order. Add stable-order and reordering tests in Task 5.
- **Unknown texture/atlas or blend state, invalid UVs, and non-finite transforms:** make descriptors ineligible rather than guessing. Add rejection tests in Tasks 2–3.
- **Level restart, exit, and object destruction:** no GameObject/CCSprite pointer may survive in a snapshot. Test repeated capture/reset and generation changes in Task 4.
- **Dynamic colors, opacity, rotations, scales, trigger-driven movement, and missing GLES capabilities:** refresh values when captured or mark the item unsupported; test changing attributes in Task 3 and absent capabilities in Task 6.

---

### Task 1: Replace the diagnostic product with a renderer-only shell

**Files:**
- Modify: `src/main.cpp`
- Modify: `mod.json`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `changelog.md`
- Modify: `.github/workflows/build-dashcore-android64.yml`
- Create: `scripts/test_renderer_product_contract.py`
- Delete: `src/bismuth/GLESComputeProbe.cpp`
- Delete: `src/bismuth/GLESComputeProbe.hpp`
- Delete: `src/bismuth/GLESObjectCullingProbe.cpp`
- Delete: `src/bismuth/GLESObjectCullingProbe.hpp`

**Interfaces:**
- Consume the existing Geode module-load entry point and pinned build configuration.
- Produce a minimal renderer-only runtime shell; later tasks add behavior behind it.
- Preserve mod ID `hesta.optgod` so the experimental package updates/replaces the existing DashCore package.
- Set package version to `0.11.0-alpha`; identify the package as DashCore Experimental Renderer and state that this milestone still uses vanilla drawing.

- [ ] **Step 1: Write the failing product-contract test**

Create `scripts/test_renderer_product_contract.py`. Assert that:
  - `mod.json` keeps ID `hesta.optgod`, uses version `0.11.0-alpha`, and has no runtime settings in this initial read-only milestone.
  - Runtime source contains no `StartupProfile`, `RenderPipelineProfile`, culling telemetry, diagnostic override-file handling, automatic last-level loading, or old GLES probe entry points.
  - Runtime source does not hook `CCNode::visit`, `CCSprite::draw`, `CCSpriteBatchNode::draw`, `CCTextureAtlas::drawQuads`, or `CCDirector::drawScene`.
  - README describes a staged level-object renderer and makes no performance or active draw-replacement claim.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python3 scripts/test_renderer_product_contract.py`

Expected: FAIL against the current diagnostic DashCore implementation.

- [ ] **Step 3: Replace the runtime with a minimal Geode shell**

Reduce `src/main.cpp` to the existing Geode module-load convention and renderer-module initialization. Remove profiling, render-pipeline telemetry, static culling, last-level auto-loading, AutoTest override-file handling, and all diagnostic/draw hooks. Do not add replacement draw hooks.

Delete the obsolete `src/bismuth` probes. Keep the Android GLES link only for the planned renderer backend; update its CMake comment to describe ordinary GLES backend support without assuming GLES 3.1/compute.

- [ ] **Step 4: Update package metadata and project description**

Update `mod.json` to `0.11.0-alpha`, retain `hesta.optgod`, remove unrelated runtime settings, and describe the product as an experimental renderer for Geometry Dash level objects. Update README/changelog to state that the first milestone is read-only and vanilla rendering remains active.

- [ ] **Step 5: Register the contract test in Android64 CI**

Run the new Python contract test from `.github/workflows/build-dashcore-android64.yml`. Keep Cppcheck and Android64 package validation enabled.

- [ ] **Step 6: Run the test and commit**

Run: `python3 scripts/test_renderer_product_contract.py`

Expected: PASS; the runtime package contains no profiling, diagnostics, culling, automatic level loading, or AutoTest feature.

Commit: `refactor: isolate DashCore experimental renderer runtime`

---

### Task 2: Add the pure renderer data model

**Files:**
- Create: `src/renderer/core/RendererTypes.hpp`
- Create: `src/renderer/core/RendererTypes.cpp`
- Create: `tests/renderer_model_tests.cpp`
- Modify: `.github/workflows/build-dashcore-android64.yml`

**Interfaces:**
- `ObjectId { std::uint32_t levelGeneration; std::uint32_t objectIndex; }`.
- `SpriteId { ObjectId owner; std::uint32_t childIndex; }`.
- `TextureKey { std::uint64_t value; }`.
- `RectF { float x; float y; float width; float height; }`.
- `Transform2D { std::array<float, 6> values; }`.
- `ColorRGBA8 { std::uint8_t r; std::uint8_t g; std::uint8_t b; std::uint8_t a; }`.
- `BlendState { std::uint32_t sourceFactor; std::uint32_t destinationFactor; bool supported; }`.
- `LayerOrder { std::int32_t zLayer; std::uint32_t traversalOrder; }`.
- `RawSpriteAttributes` owns texture key, UV/local rectangles, transform, RGBA color, opacity, blend state, and visibility values copied during the current source read.
- `SpriteDescriptor` adds `ObjectId`, `SpriteId`, and `LayerOrder` to the validated render attributes.
- `RenderSnapshot` owns level generation, frame index, and `std::vector<SpriteDescriptor>`.
- `bool validateSpriteDescriptor(SpriteDescriptor const&) noexcept`.
- Snapshot/descriptors contain value types only; never store `GameObject*`, `CCNode*`, `CCSprite*`, or any pointer into the live scene.

- [ ] **Step 1: Write failing model tests**

In `tests/renderer_model_tests.cpp`, test finite/supported descriptors, NaN/infinite transforms, invalid/empty rectangles, unsupported texture/blend states, and object/sprite ID ownership and generation.

- [ ] **Step 2: Run tests and verify they fail**

Run: `c++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/renderer_model_tests.cpp src/renderer/core/*.cpp -o /tmp/dashcore-renderer-model-tests && /tmp/dashcore-renderer-model-tests`

Expected: compile/test failure because the model does not yet exist.

- [ ] **Step 3: Implement the value types and validation**

Implement the declared structures and `validateSpriteDescriptor`. Reject non-finite geometry/transforms, invalid rectangles, and unsupported resource/blend states without dereferencing engine objects.

- [ ] **Step 4: Run tests and commit**

Run the same host C++ test command.

Expected: PASS without requiring Geode SDK or Android headers.

Commit: `feat: add renderer snapshot data model`

---

### Task 3: Extract sprite descriptors from copied source attributes

**Files:**
- Create: `src/renderer/core/SpriteDescriptorExtractor.hpp`
- Create: `src/renderer/core/SpriteDescriptorExtractor.cpp`
- Modify: `tests/renderer_model_tests.cpp`

**Interfaces:**
- `bool extractSpriteDescriptor(RawSpriteAttributes const& attributes, ObjectId owner, std::uint32_t childIndex, LayerOrder order, SpriteDescriptor& output) noexcept`.
- The extractor is pure C++ and does not include Geode/Cocos headers.
- An attribute is unsupported when texture identity, UV/local bounds, transform, color, opacity, visibility, or blend state cannot be read reliably by the source adapter.
- The extractor never receives or stores a live engine pointer.

- [ ] **Step 1: Add failing extraction fixtures**

Test valid base/detail/glow/black-style sprite descriptors, multiple child indices under one owner, and changed values between captures for position/rotation/scale, RGBA, opacity, and visibility. Add unknown texture, unsupported blend, invalid UV, and non-finite fixtures.

- [ ] **Step 2: Run tests and verify they fail**

Run the host C++ model-test command.

Expected: FAIL because the extractor does not exist.

- [ ] **Step 3: Implement the pure extractor**

Copy validated fields from `RawSpriteAttributes` to `SpriteDescriptor`; call descriptor validation; return false for incomplete or unsupported attributes without fabricating defaults.

- [ ] **Step 4: Run tests and commit**

Expected: PASS; a second extraction reflects changed input attributes, and invalid/unsupported values remain ineligible.

Commit: `feat: validate and extract sprite descriptors`

---

### Task 4: Build a lifecycle-safe level-object collector

**Files:**
- Create: `src/renderer/core/LevelObjectSource.hpp`
- Create: `src/renderer/core/LevelObjectCollector.hpp`
- Create: `src/renderer/core/LevelObjectCollector.cpp`
- Create: `src/renderer/geode/GeodeLevelObjectSource.hpp`
- Create: `src/renderer/geode/GeodeLevelObjectSource.cpp`
- Modify: `tests/renderer_model_tests.cpp`

**Interfaces:**
- `LevelObjectSource` exposes a synchronous collection operation that fills caller-owned `std::vector<SpriteDescriptor>` and returns failure when the source is incomplete.
- `LevelObjectCollector` exposes:
  - `bool capture(LevelObjectSource const&, std::uint64_t frameIndex, RenderSnapshot& output) const`;
  - `void reset(std::uint32_t nextLevelGeneration) noexcept`.
- `GeodeLevelObjectSource` adapts a live `PlayLayer` to `LevelObjectSource`. Engine pointers are used only during the collection call and never copied into snapshots.
- Use the verified `PlayLayer::m_objects` inventory and Cocos child traversal; do not use `ObjectToolbox::allKeys()` as an Android enumeration source.

- [ ] **Step 1: Add failing collector tests**

Use fake sources to test empty levels, multiple child sprites under one GameObject, nested child nodes/batch-node ancestry, null/unsupported entries, repeated capture/reset, and level-generation changes. Assert that no pointer-like source identity appears in snapshot data.

- [ ] **Step 2: Run tests and verify they fail**

Run the host C++ model-test command.

Expected: FAIL because the source/collector interfaces do not exist.

- [ ] **Step 3: Implement the pure collector**

Implement `LevelObjectSource` and `LevelObjectCollector`. Rebuild each snapshot from owned descriptor values; assign object/child indices within the current level generation; reject incomplete entries rather than inventing attributes.

- [ ] **Step 4: Implement the Geode source adapter**

Enumerate `PlayLayer::m_objects` and traverse each object’s Cocos child hierarchy synchronously. Guard null children and traversal limits, avoid double-counting nested roots, copy required sprite attributes into `RawSpriteAttributes`, then call the Task 3 extractor.

Before using each Geode method, verify its declaration and Android64 availability in the pinned bindings. If a method is unavailable, exclude that data path and document the unsupported case. Do not modify the Cocos tree or call draw/visibility setters.

- [ ] **Step 5: Run tests and build Android64**

Run the host C++ tests and Android64 build workflow.

Expected: fake-source tests PASS and the Geode source adapter compiles for `arm64-v8a`.

- [ ] **Step 6: Commit**

Commit: `feat: collect level object snapshots safely`

---

### Task 5: Reproduce layer order and plan only safe contiguous batches

**Files:**
- Create: `src/renderer/core/LayerOrderModel.hpp`
- Create: `src/renderer/core/LayerOrderModel.cpp`
- Create: `src/renderer/core/BatchPlanner.hpp`
- Create: `src/renderer/core/BatchPlanner.cpp`
- Modify: `tests/renderer_model_tests.cpp`

**Interfaces:**
- `std::vector<std::uint32_t> buildStableDrawOrder(std::span<SpriteDescriptor const> sprites)`.
- `BatchKey` contains exact layer, texture key, and blend state required for compatibility.
- `RenderBatch` contains a key and a contiguous range in the stable draw-order array.
- `std::vector<RenderBatch> planBatches(std::span<SpriteDescriptor const> sprites, std::span<std::uint32_t const> drawOrder)`.
- Batches combine only adjacent compatible sprites in observed order; no global sort may move a sprite past another sprite.

- [ ] **Step 1: Add failing order/batch tests**

Cover different z-layers, equal-z sprites with different traversal order, same texture with different blend states, same blend with different textures, and a compatibility break between otherwise matching sprites.

- [ ] **Step 2: Run tests and verify they fail**

Run the host C++ model-test command.

Expected: FAIL for the absent order and batch planner.

- [ ] **Step 3: Verify the source-order fields**

Use the pinned Cocos bindings and observed child-list order to determine the non-mutating source of z-order and arrival/traversal order. Do not call a sorting function that mutates the live scene. If exact order cannot be reconstructed, mark that subtree unsupported.

- [ ] **Step 4: Implement stable ordering and contiguous batching**

Implement `buildStableDrawOrder` and `planBatches`. Preserve input order for equal-layer items and split batches whenever layer, texture, or blend state changes.

- [ ] **Step 5: Run tests and commit**

Expected: tests prove that batching never crosses a layer/order/blend boundary and that source reordering produces a corresponding new plan.

Commit: `feat: preserve draw order in renderer batch planner`

---

### Task 6: Prepare geometry and define a GLES-safe backend

**Files:**
- Create: `src/renderer/core/GeometryBuilder.hpp`
- Create: `src/renderer/core/GeometryBuilder.cpp`
- Create: `src/renderer/core/RenderBackend.hpp`
- Create: `src/renderer/gles/GLESBackend.hpp`
- Create: `src/renderer/gles/GLESBackend.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/renderer_model_tests.cpp`

**Interfaces:**
- `Vertex` contains position, UV, and vertex color as explicit value fields.
- `GeometryData` owns vertex and index arrays; it contains no live Cocos pointers.
- `bool buildGeometry(RenderSnapshot const&, std::span<RenderBatch const>, GeometryData& output) noexcept`.
- `GlesCapabilities` records the actual GLES version and individually detected optional features, including compute/SSBO support.
- `GlesCapabilities queryGlesCapabilities() noexcept` is called only while a valid GL context is current.
- `RenderBackend` exposes initialization, resource preparation/upload, and release; it has no draw/present method in this milestone.

- [ ] **Step 1: Add failing geometry tests**

Test quad vertex/index counts, UV propagation, finite positions, index overflow rejection, unsupported descriptor exclusion, and an empty snapshot.

- [ ] **Step 2: Add capability-selection tests**

Test capability records with no compute/SSBO support, GLES 3.x without optional extensions, and unavailable context information. Expected: CPU/ordinary GLES preparation remains possible where supported; compute is never required.

- [ ] **Step 3: Run tests and verify they fail**

Run the host C++ model-test command.

Expected: FAIL for missing geometry/backend components.

- [ ] **Step 4: Implement CPU geometry preparation**

Build geometry from owned descriptors and planned batches. Reject malformed geometry and buffer sizes that exceed the chosen index representation. Do not issue draw calls.

- [ ] **Step 5: Implement runtime GLES capability detection**

Query the current context version/extensions through GLES APIs. Do not infer capabilities from device model or assume desktop GL 4.3 features exist on Android.

- [ ] **Step 6: Implement the preparation-only GLES backend**

Create/release required buffers and upload prepared data only when the context supports the selected path. Keep a CPU-only path for model/parity work. No shaders, compute dispatches, indirect draws, or framebuffer presentation are enabled in this task.

- [ ] **Step 7: Run model tests and Android64 build; commit**

Expected: host tests PASS and Android64 build succeeds without making compute/SSBO a startup requirement.

Commit: `feat: prepare renderer geometry and GLES capabilities`

---

### Task 7: Integrate the parallel pipeline with level lifecycle

**Files:**
- Create: `src/renderer/geode/RendererCoordinator.hpp`
- Create: `src/renderer/geode/RendererCoordinator.cpp`
- Create: `src/renderer/geode/PlayLayerRendererHooks.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `scripts/test_renderer_product_contract.py`

**Interfaces:**
- `RendererCoordinator` exposes:
  - `void onLevelInit(PlayLayer* layer) noexcept`;
  - `bool onLevelReady(PlayLayer* layer) noexcept`;
  - `bool onFrame(PlayLayer* layer, std::uint64_t frameIndex) noexcept`;
  - `void onLevelExit(PlayLayer* layer) noexcept`;
  - `RenderSnapshot const* snapshot() const noexcept`.
- `onFrame` refreshes dynamic sprite values after the original `PlayLayer::postUpdate(float)` callback; it does not intercept draw calls.
- The Geode hook adapter forwards each original callback exactly once.
- `onLevelReady` may capture and prepare a snapshot only; it must not replace rendering or mutate game objects.

- [ ] **Step 1: Add failing integration-contract tests**

Assert that the coordinator resets on a new level generation, captures only the active PlayLayer, refreshes transform/color/opacity values on successive frames, and never registers hooks for Cocos drawing callbacks.

- [ ] **Step 2: Verify lifecycle hook availability**

Confirm exact Android64 support/signatures for the selected `PlayLayer` lifecycle callbacks against pinned Geode bindings. Use only confirmed methods. If a cleanup callback is unavailable, keep no live engine pointers in coordinator state and release owned snapshot data at the next verified lifecycle boundary.

- [ ] **Step 3: Implement coordinator lifecycle**

Reset collector/snapshot state at level initialization; capture after level setup completes; refresh dynamic values after each original `PlayLayer::postUpdate(float)` call; release owned snapshot/backend resources at the verified level-exit boundary. Preserve original callback order and call each original exactly once.

- [ ] **Step 4: Keep the mode read-only**

Default to parallel/observe-only mode. The only per-frame integration hook in this milestone is the verified `PlayLayer::postUpdate(float)` lifecycle/update callback. Do not install `CCNode::visit`, `CCSprite::draw`, batch, atlas, or director draw hooks. Do not call any draw/present API from the new backend.

- [ ] **Step 5: Run contract tests and Android64 build**

Expected: tests PASS; the mod loads and the only visible renderer remains vanilla.

- [ ] **Step 6: Commit**

Commit: `feat: integrate read-only renderer pipeline lifecycle`

---

### Task 8: Add structural parity gates and validate the first alpha package

**Files:**
- Create: `src/renderer/core/RendererParity.hpp`
- Create: `src/renderer/core/RendererParity.cpp`
- Modify: `tests/renderer_model_tests.cpp`
- Modify: `scripts/test_renderer_product_contract.py`
- Modify: `.github/workflows/build-dashcore-android64.yml`
- Modify: `.github/workflows/autotest-v3-cloud.yml`
- Replace or simplify: `scripts/autotest_gameplay_diagnostics.py` and its test so the CI validator does not expect diagnostic markers.

**Interfaces:**
- `ParityReport validateSnapshot(RenderSnapshot const& snapshot, std::span<RenderBatch const> batches, GeometryData const& geometry) noexcept`.
- The report contains pass/fail and explicit failure reasons for unsupported descriptors, duplicate sprite IDs, order loss, invalid batches, and malformed geometry.
- AutoTest remains external CI tooling and validates package load, level entry/play, restart/exit, and absence of crashes. It does not request runtime diagnostic modes or require profiling logs.

- [ ] **Step 1: Add failing parity tests**

Test empty/dense snapshots, duplicate sprite IDs, missing descriptors, layer-order changes, incorrect batch ranges, geometry size mismatch, unsupported resource states, and level-generation mismatch.

- [ ] **Step 2: Run tests and verify they fail**

Run the host C++ model-test command.

Expected: FAIL for the absent parity validator.

- [ ] **Step 3: Implement structural parity validation**

Validate that every candidate sprite is represented once, every batch references a valid contiguous range, order is preserved, and geometry counts match candidate descriptors. Treat any unclassified difference as a failure, not a warning.

This milestone checks structure and prepared geometry only. It does not claim pixel-perfect visual parity, GPU performance, or an FPS improvement.

- [ ] **Step 4: Simplify cloud AutoTest to gameplay-only validation**

Update the workflow to run against `experimental-renderer` and select the exact build SHA. Remove its dependency on diagnostic/culling/profile markers and runtime override files. Keep package validation, game launch, level play, restart/exit, screenshot capture, and crash detection.

- [ ] **Step 5: Run all gates for the exact branch commit**

Run Python product-contract tests, host C++ renderer model tests, Cppcheck, Android64 Release build with minimum SDK 29, and cloud gameplay AutoTest against the exact build SHA.

Expected: all gates pass. AutoTest confirms basic stability only and is not treated as physical-device performance evidence.

- [ ] **Step 6: Perform the physical-device smoke test**

Install the exact `0.11.0-alpha` artifact on the Huawei Y9s. Enter, restart, and exit levels containing multiple sprite layers, glow/detail sprites, groups, triggers, and animated objects. Confirm appearance and gameplay remain vanilla because the experimental pipeline does not draw.

Record the artifact SHA and Geode log. Stop and revert the candidate if crashes, freezes, missing objects, or altered gameplay occur.

- [ ] **Step 7: Commit the parity gate**

Commit: `test: validate parallel renderer snapshots and gameplay`

## Completion Gate

This plan is complete only when Tasks 1–8 pass. The deliverable is a renderer-only `0.11.0-alpha` package with a read-only parallel pipeline, deterministic value snapshots, conservative batching/geometry preparation, runtime GLES capability detection, vanilla-only visible rendering, and a working fallback path.

**Not included in this plan:** the first experimental draw replacement, migration of any object category, culling, compute-shader optimization, batching-performance claims, or a non-alpha optimization release. After this milestone is reviewed and physically validated, create a separate implementation plan for Stage 5 (one small static object category with an opt-in switch and automatic vanilla fallback). Only after renderer equivalence is established should a later plan cover optimization.
