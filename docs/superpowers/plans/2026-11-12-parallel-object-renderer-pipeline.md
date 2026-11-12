# Parallel Object Renderer Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Build the first safe DashCore experimental-renderer milestone: a read-only, parallel pipeline that captures level-object sprites, preserves source ordering, prepares compatible batches and geometry, and never replaces or issues game draw calls.

**Architecture:** Keep the Geode-facing collector separate from pure C++20 data, ordering, batching, and geometry modules. The Android GLES backend may detect capabilities and prepare resources, but this milestone stays in observe/compare mode; all actual rendering remains vanilla. Unsupported or incomplete data is excluded from the experimental pipeline.

**Tech Stack:** C++20, Geode SDK 5.11.2, Geometry Dash 2.2081 bindings, Android64 (\`arm64-v8a\`), Android API 29 minimum, OpenGL ES capability queries, GitHub Actions, Python 3 standard-library tests, host C++ compiler for pure-model tests.

**Spec:** \`docs/superpowers/specs/2026-11-12-experimental-renderer-design.md\`

## Global Constraints

- Target Geometry Dash 2.2081, Geode 5.11.2, Android64 (\`arm64-v8a\`), Android 10 / API 29 minimum.
- Physical reference device: Huawei Y9s, Kirin 710F, Mali-G51 MP4.
- Work only on \`experimental-renderer\`; do not modify or merge into \`main\`.
- The product scope is renderer compatibility and level-object rendering only.
- Global shaders/effects, backgrounds, particles outside the validated scope, UI, menus, HUD, editor, and player rendering remain vanilla.
- Do not change physics, collisions, triggers, group logic, gameplay timing, input, or object behavior.
- No profiling, performance-diagnostics, culling, or AutoTest feature may ship in the mod.
- Build tests, static checks, and cloud gameplay automation may remain as development/CI tooling; they must not become runtime mod features.
- No draw interception or experimental draw call is allowed in this milestone.
- Keep vanilla drawing as the only visible output; no double drawing.
- Preserve source draw order; never reorder across layers or incompatible blending states to improve batching.
- Unknown resources, unsupported blending, incomplete sprite data, and invalid transforms stay outside the experimental candidate set.
- Use only verified Geode bindings for the pinned Android64 target; do not guess offsets or hook unsupported/inline methods.
- Detect GLES version/extensions at runtime. Compute shaders, SSBOs, and indirect drawing are optional future capabilities, never startup requirements.
- Do not copy or link Bismuth source code; all implementation code and data structures must be original.
- Every experimental or validation package remains prerelease \`alpha\`; no optimization claim is allowed without repeatable physical-device evidence.

## Review Focus

- **Multiple sprites under one GameObject or nested batch nodes:** the collector must enumerate each supported sprite once and must not assume one GameObject equals one sprite. Add fixtures with multiple children and nested batch ancestry in Tasks 3–4.
- **Child reordering and z-order changes:** the planner must preserve the observed vanilla order, including equal-z arrival order; add stable-order and reordering tests in Task 5.
- **Unknown texture/atlas or blend state, invalid UVs, and non-finite transforms:** these must make a descriptor ineligible rather than crash or guess; add rejection tests in Tasks 2 and 4.
- **Level restart, level exit, and object destruction:** no GameObject/CCSprite pointer may survive in a snapshot; test repeated capture/reset and generation changes in Task 3.
- **Dynamic colors, opacity, rotations, scales, and trigger-driven movement:** values must be refreshed or explicitly classified unsupported; add changing-state fixtures in Task 4. Also test absent GLES capabilities in Task 6.

---

### Task 1: Replace the diagnostic product with a renderer-only shell

**Files:**
- Modify: \`src/main.cpp\`
- Modify: \`mod.json\`
- Modify: \`CMakeLists.txt\`
- Modify: \`README.md\`
- Modify: \`changelog.md\`
- Modify: \`.github/workflows/build-dashcore-android64.yml\`
- Create: \`scripts/test_renderer_product_contract.py\`
- Delete: \`src/bismuth/GLESComputeProbe.cpp\`
- Delete: \`src/bismuth/GLESComputeProbe.hpp\`
- Delete: \`src/bismuth/GLESObjectCullingProbe.cpp\`
- Delete: \`src/bismuth/GLESObjectCullingProbe.hpp\`

**Interfaces:**
- Consumes: the existing Geode module-load entry point and pinned build configuration.
- Produces: a minimal renderer-only module shell; the following tasks add implementation behind that shell.
- Preserve mod ID \`hesta.optgod\` so the experimental package replaces the existing DashCore package rather than installing as an unrelated second mod.
- Set the experimental package version to \`0.11.0-alpha\`; set its display name/description to identify the experimental object renderer and state that this milestone still uses vanilla drawing.

- [ ] **Step 1: Write the failing product-contract test**

Create \`scripts/test_renderer_product_contract.py\`. Assert that:
  - \`mod.json\` keeps ID \`hesta.optgod\`, uses version \`0.11.0-alpha\`, and has no settings for diagnostics, profiling, culling, or automatic last-level loading.
  - Runtime source contains no \`StartupProfile\`, \`RenderPipelineProfile\`, culling telemetry, diagnostic override-file handling, or the old \`GLES*Probe\` entry points.
  - Runtime source does not hook \`CCNode::visit\`, \`CCSprite::draw\`, \`CCSpriteBatchNode::draw\`, \`CCTextureAtlas::drawQuads\`, or \`CCDirector::drawScene\`.
  - The README describes a staged level-object renderer and does not claim an optimization or active draw replacement.

- [ ] **Step 2: Run the contract test and verify it fails**

Run: \`python3 scripts/test_renderer_product_contract.py\`

Expected: FAIL against the current diagnostic DashCore implementation.

- [ ] **Step 3: Replace the runtime with the minimal renderer shell**

Reduce \`src/main.cpp\` to the existing Geode module-load convention and a call into the renderer module. Remove performance profiling, render-pipeline telemetry, static culling, last-level auto-loading, AutoTest override files, and all diagnostic/draw hooks. Do not add any replacement draw hook.

Delete the obsolete \`src/bismuth\` probes. Keep the GLES link only if the new backend will use GLES APIs; update its CMake comment to describe the renderer backend and make no GLES 3.1/compute assumption.

- [ ] **Step 4: Update package metadata and project description**

Update \`mod.json\` to \`0.11.0-alpha\`, retain \`hesta.optgod\`, remove unrelated runtime settings, and describe the product as an experimental renderer for Geometry Dash level objects. Update README/changelog to state that the first milestone is read-only and vanilla rendering remains active.

- [ ] **Step 5: Register the contract test in Android64 CI**

Run the new Python contract test from \`.github/workflows/build-dashcore-android64.yml\`. Keep Cppcheck and Android64 package validation enabled.

- [ ] **Step 6: Run the contract test and commit**

Run: \`python3 scripts/test_renderer_product_contract.py\`

Expected: PASS; no profiling, diagnostics, culling, or AutoTest feature remains in the runtime package.

Commit: \`refactor: isolate DashCore experimental renderer runtime\`

---

### Task 2: Add the pure renderer data model

**Files:**
- Create: \`src/renderer/RendererTypes.hpp\`
- Create: \`src/renderer/RendererTypes.cpp\`
- Create: \`tests/renderer_model_tests.cpp\`
- Modify: \`.github/workflows/build-dashcore-android64.yml\`

**Interfaces:**
- Produces:
  - \`ObjectId { std::uint32_t levelGeneration; std::uint32_t objectIndex; }\`
  - \`SpriteId { ObjectId owner; std::uint32_t childIndex; }\`
  - \`TextureKey { std::uint64_t value; }\`
  - \`RectF { float x; float y; float width; float height; }\`
  - \`Transform2D { std::array<float, 6> values; }\`
  - \`BlendState { std::uint32_t sourceFactor; std::uint32_t destinationFactor; bool supported; }\`
  - \`LayerOrder { std::int32_t zLayer; std::uint32_t traversalOrder; }\`
  - \`SpriteDescriptor\` containing IDs, texture key, UV rectangle, local bounds, transform, RGBA color, opacity, blend state, layer order, and a supported/visible state.
  - \`RenderSnapshot\` containing level generation, frame index, and owned \`std::vector<SpriteDescriptor>\`.
  - \`bool validateSpriteDescriptor(SpriteDescriptor const&) noexcept\`.
- Snapshot data must contain value types only. It must not store \`GameObject*\`, \`CCNode*\`, \`CCSprite*\`, or any other pointer into the live scene.

- [ ] **Step 1: Write failing model tests**

In \`tests/renderer_model_tests.cpp\`, add tests for:
  - a finite, supported descriptor being accepted;
  - NaN/infinite transform values being rejected;
  - invalid or empty UV/local-bound rectangles being rejected;
  - unsupported blend/resource state being rejected for experimental processing;
  - object/sprite IDs preserving their level generation and owner relationship.

- [ ] **Step 2: Run the model tests and verify they fail**

Run: \`c++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/renderer_model_tests.cpp src/renderer/RendererTypes.cpp -o /tmp/dashcore-renderer-model-tests && /tmp/dashcore-renderer-model-tests\`

Expected: compile/test failure because the model does not yet exist.

- [ ] **Step 3: Implement the value types and validation**

Implement the declared types and \`validateSpriteDescriptor\` in \`RendererTypes.hpp/.cpp\`. Validation must reject non-finite geometry/transforms, invalid rectangles, and unsupported resource/blend states without dereferencing engine objects.

- [ ] **Step 4: Run the model tests and commit**

Run the same host C++ test command.

Expected: PASS without requiring the Geode SDK or Android headers.

Commit: \`feat: add renderer snapshot data model\`

---

### Task 3: Build a lifecycle-safe level-object collector

**Files:**
- Create: \`src/renderer/LevelObjectSource.hpp\`
- Create: \`src/renderer/LevelObjectCollector.hpp\`
- Create: \`src/renderer/LevelObjectCollector.cpp\`
- Create: \`src/renderer/geode/GeodeLevelObjectSource.hpp\`
- Create: \`src/renderer/geode/GeodeLevelObjectSource.cpp\`
- Modify: \`tests/renderer_model_tests.cpp\`

**Interfaces:**
- \`class LevelObjectSource\` exposes a synchronous collection operation that fills caller-owned descriptor values and returns failure when the source is incomplete.
- \`class LevelObjectCollector\` exposes:
  - \`bool capture(LevelObjectSource const&, std::uint64_t frameIndex, RenderSnapshot& output) const\`;
  - \`void reset(std::uint32_t nextLevelGeneration) noexcept\`.
- \`GeodeLevelObjectSource\` adapts a live \`PlayLayer\` to \`LevelObjectSource\`; any engine pointers are read only during the call and are never copied into \`RenderSnapshot\`.
- Use the verified \`PlayLayer::m_objects\` inventory and Cocos child traversal. Do not use \`ObjectToolbox::allKeys()\` as an Android enumeration source.

- [ ] **Step 1: Add failing collector tests**

Add fake-source tests for:
  - empty levels;
  - a GameObject producing multiple child sprites;
  - nested child nodes and batch-node ancestry;
  - a null/unsupported source entry being excluded without aborting the complete level;
  - two successive level generations not sharing object IDs;
  - repeated capture/reset not retaining pointers or stale descriptors.

- [ ] **Step 2: Run the collector tests and verify they fail**

Run the host C++ model-test command from Task 2.

Expected: FAIL because the source/collector interfaces are not implemented.

- [ ] **Step 3: Implement the pure collector**

Implement \`LevelObjectSource\` and \`LevelObjectCollector\`. Rebuild each snapshot from value data; assign object and child indices for the current level generation; reject incomplete entries rather than inventing missing attributes.

- [ ] **Step 4: Implement the Geode adapter from verified bindings**

Implement \`GeodeLevelObjectSource\` using the pinned 2.2081 bindings and current source inventory. Traverse each object’s child hierarchy synchronously, guard null children and depth/count limits, and avoid double-counting nested roots. Do not modify the Cocos tree or call any draw/visibility setters.

Before using each Geode method, verify its declaration and Android64 availability in the pinned bindings. If a required method is not available, exclude that data path and document the unsupported case.

- [ ] **Step 5: Run tests and build Android64**

Run the host C++ tests and the Android64 build workflow.

Expected: fake-source tests PASS and the Geode adapter compiles for \`arm64-v8a\`.

- [ ] **Step 6: Commit**

Commit: \`feat: collect level object snapshots safely\`

---

### Task 4: Extract complete sprite descriptors and refresh dynamic values

**Files:**
- Create: \`src/renderer/geode/SpriteDescriptorExtractor.hpp\`
- Create: \`src/renderer/geode/SpriteDescriptorExtractor.cpp\`
- Modify: \`src/renderer/geode/GeodeLevelObjectSource.cpp\`
- Modify: \`tests/renderer_model_tests.cpp\`

**Interfaces:**
- \`bool extractSpriteDescriptor(cocos2d::CCSprite* sprite, ObjectId owner, std::uint32_t childIndex, LayerOrder order, SpriteDescriptor& output) noexcept\`.
- The extractor reports unsupported when texture identity, texture rectangle/UV, local bounds, transform, color, opacity, or blend state cannot be read reliably.
- The extractor never stores the input sprite pointer in the output descriptor.

- [ ] **Step 1: Add failing descriptor-extraction fixtures**

Test multiple sprites per GameObject and changes between snapshots to position/rotation/scale, RGBA color, opacity, visibility, UV rectangle, and blend state. Add fixtures for unknown texture and non-finite values.

- [ ] **Step 2: Verify the extraction tests fail**

Run the host C++ model-test command. Expected: FAIL for missing extractor/test adapter.

- [ ] **Step 3: Verify the pinned sprite bindings**

Inspect the exact 2.2081 \`CCSprite\`, \`CCNode\`, \`CCTexture2D\`, and blend declarations used by the implementation. Do not infer texture atlas membership from a batch-node ancestor alone.

- [ ] **Step 4: Implement descriptor extraction**

Read attributes through verified Geode bindings. Use the actual current sprite values for every capture; do not freeze color/opacity/transform values at level load. Mark a sprite unsupported when a required attribute is ambiguous.

- [ ] **Step 5: Add dynamic-state regression tests and commit**

Expected: a second capture reflects changed transform/color/opacity values; unsupported values remain ineligible and do not crash the collector.

Commit: \`feat: extract safe sprite render descriptors\`

---

### Task 5: Reproduce layer order and plan only safe contiguous batches

**Files:**
- Create: \`src/renderer/LayerOrderModel.hpp\`
- Create: \`src/renderer/LayerOrderModel.cpp\`
- Create: \`src/renderer/BatchPlanner.hpp\`
- Create: \`src/renderer/BatchPlanner.cpp\`
- Modify: \`tests/renderer_model_tests.cpp\`

**Interfaces:**
- \`std::vector<std::uint32_t> buildStableDrawOrder(std::span<SpriteDescriptor const> sprites)\`.
- \`BatchKey\` contains the exact layer, texture key, and blend state required for compatibility.
- \`RenderBatch\` contains a key and a contiguous range in the stable draw-order array.
- \`std::vector<RenderBatch> planBatches(std::span<SpriteDescriptor const> sprites, std::span<std::uint32_t const> drawOrder)\`.
- Batches may combine only adjacent compatible sprites in the observed order; no global sort may move a sprite past another sprite.

- [ ] **Step 1: Add failing order/batch tests**

Cover different z-layers, equal-z sprites with different traversal order, same texture with different blend states, same blend with different textures, and a compatibility break between otherwise matching sprites.

- [ ] **Step 2: Run tests and verify they fail**

Run the host C++ model-test command.

Expected: FAIL for missing order and batch planner.

- [ ] **Step 3: Verify the source-order fields**

Use the pinned Cocos bindings and observed child-list ordering to determine the non-mutating source of z-order and arrival/traversal order. Do not call a sorting function that mutates the live scene. If exact order cannot be reconstructed, mark that subtree unsupported.

- [ ] **Step 4: Implement stable ordering and contiguous batching**

Implement \`buildStableDrawOrder\` and \`planBatches\`. Preserve the exact input order for equal-layer items and split batches whenever layer, texture, or blend state changes.

- [ ] **Step 5: Run tests and commit**

Expected: tests prove that batching never crosses a layer/order/blend boundary and that reordering the source produces a corresponding new draw-order plan.

Commit: \`feat: preserve draw order in renderer batch planner\`

---

### Task 6: Prepare geometry and detect Android GLES capabilities safely

**Files:**
- Create: \`src/renderer/GeometryBuilder.hpp\`
- Create: \`src/renderer/GeometryBuilder.cpp\`
- Create: \`src/renderer/RenderBackend.hpp\`
- Create: \`src/renderer/GLESBackend.hpp\`
- Create: \`src/renderer/GLESBackend.cpp\`
- Modify: \`CMakeLists.txt\`
- Modify: \`tests/renderer_model_tests.cpp\`

**Interfaces:**
- \`Vertex\` contains position, UV, and vertex color as explicit value fields.
- \`GeometryData\` owns vertex and index arrays; it contains no live Cocos pointers.
- \`bool buildGeometry(RenderSnapshot const&, std::span<RenderBatch const>, GeometryData& output) noexcept\`.
- \`GlesCapabilities\` records the actual GLES version and individually detected optional features, including compute/SSBO support.
- \`GlesCapabilities queryGlesCapabilities() noexcept\` is called only while a valid GL context is current.
- \`RenderBackend\` exposes initialization, resource preparation/upload, and release; it has no draw/present method in this milestone.

- [ ] **Step 1: Add failing geometry tests**

Test quad vertex/index counts, UV propagation, finite positions, index overflow rejection, unsupported descriptor exclusion, and an empty snapshot.

- [ ] **Step 2: Add capability-selection tests**

Test GLES capability records with no compute/SSBO support, GLES 3.x without optional extensions, and a context where capability information is unavailable. Expected: CPU/ordinary GLES preparation remains available where supported; compute is never required.

- [ ] **Step 3: Run tests and verify failures**

Run the host C++ model-test command.

Expected: FAIL for missing geometry/backend components.

- [ ] **Step 4: Implement CPU geometry preparation**

Build geometry from owned descriptors and planned batches. Reject malformed geometry and buffer sizes that exceed the chosen index representation. Do not issue draw calls.

- [ ] **Step 5: Implement runtime GLES capability detection**

Query the current context version/extensions through GLES APIs. Do not parse assumptions from the device model or assume that desktop GL 4.3 features exist on Android.

- [ ] **Step 6: Implement the preparation-only GLES backend**

Create/release required buffers and upload prepared data only when the context supports the selected path. Keep a CPU-only path for model/parity work. No shaders, compute dispatches, indirect draws, or framebuffer presentation are enabled in this task.

- [ ] **Step 7: Run model tests and Android64 build; commit**

Expected: host tests PASS and Android64 build succeeds without making compute/SSBO a startup requirement.

Commit: \`feat: prepare renderer geometry and GLES capabilities\`

---

### Task 7: Integrate the parallel pipeline with level lifecycle

**Files:**
- Create: \`src/renderer/RendererCoordinator.hpp\`
- Create: \`src/renderer/RendererCoordinator.cpp\`
- Create: \`src/renderer/geode/PlayLayerRendererHooks.cpp\`
- Modify: \`src/main.cpp\`
- Modify: \`CMakeLists.txt\`
- Modify: \`scripts/test_renderer_product_contract.py\`

**Interfaces:**
- \`RendererCoordinator\` exposes:
  - \`void onLevelInit(PlayLayer* layer) noexcept\`;
  - \`bool onLevelReady(PlayLayer* layer) noexcept\`;
  - \`void onLevelExit(PlayLayer* layer) noexcept\`;
  - \`RenderSnapshot const* snapshot() const noexcept\`.
- The Geode hook adapter forwards each original callback exactly once.
- \`onLevelReady\` may capture and prepare the snapshot only; it must not replace rendering or mutate game objects.

- [ ] **Step 1: Add failing integration-contract tests**

Assert that the coordinator only runs in the active PlayLayer lifecycle, resets on a new level generation, and never registers hooks for Cocos drawing callbacks.

- [ ] **Step 2: Verify lifecycle hook availability**

Confirm the exact Android64 support and signatures for the chosen \`PlayLayer\` lifecycle callbacks against the pinned Geode bindings. Use only confirmed methods. If a cleanup callback is unavailable, keep no live engine pointers in coordinator state and use the next verified lifecycle boundary to release owned snapshot data.

- [ ] **Step 3: Implement coordinator lifecycle**

Reset collector/snapshot state at level initialization; capture after level setup has completed; release owned snapshot/backend resources at the verified level-exit boundary. Preserve the original callback order and call each original exactly once.

- [ ] **Step 4: Keep the mode read-only**

The default mode is parallel/observe-only. Do not install a \`CCNode::visit\`, \`CCSprite::draw\`, batch, atlas, or director draw hook. Do not call any draw/present API from the new backend.

- [ ] **Step 5: Run contract tests and Android64 build**

Expected: tests PASS; the mod loads and the only visible renderer remains vanilla.

- [ ] **Step 6: Commit**

Commit: \`feat: integrate read-only renderer pipeline lifecycle\`

---

### Task 8: Add parity gates and validate the first alpha package

**Files:**
- Create: \`src/renderer/RendererParity.hpp\`
- Create: \`src/renderer/RendererParity.cpp\`
- Modify: \`tests/renderer_model_tests.cpp\`
- Create: \`scripts/test_renderer_parity.py\`
- Modify: \`.github/workflows/build-dashcore-android64.yml\`
- Modify: \`.github/workflows/autotest-v3-cloud.yml\`
- Modify: \`scripts/autotest_gameplay_diagnostics.py\` or replace it with a gameplay-only validator that does not expect diagnostic markers.

**Interfaces:**
- \`ParityReport validateSnapshot(RenderSnapshot const& snapshot, std::span<RenderBatch const> batches, GeometryData const& geometry) noexcept\`.
- The report contains pass/fail and explicit failure reasons for unsupported descriptors, duplicate sprite IDs, order loss, invalid batches, and malformed geometry.
- AutoTest remains external CI tooling and validates package load, level entry/play, restart/exit, and absence of crashes; it does not request a diagnostic mode or require profiling logs.

- [ ] **Step 1: Add failing parity tests**

Test empty and dense snapshots, duplicate sprite IDs, missing descriptors, layer-order changes, incorrect batch ranges, geometry size mismatch, unsupported resource states, and level-generation mismatch.

- [ ] **Step 2: Run tests and verify they fail**

Run the host C++ model-test command and \`python3 scripts/test_renderer_parity.py\`.

Expected: FAIL for the absent parity validator.

- [ ] **Step 3: Implement structural parity validation**

Validate that every candidate sprite is represented once, every batch references a valid contiguous range, order is preserved, and geometry counts match the candidate descriptors. Treat any unclassified difference as a failure, not a warning.

This milestone checks structure and prepared geometry only. It does not claim pixel-perfect visual parity, GPU performance, or an FPS improvement.

- [ ] **Step 4: Simplify cloud AutoTest to gameplay-only validation**

Update the workflow to run against \`experimental-renderer\`. Remove its dependency on diagnostic/culling/profile markers and any runtime override files. Keep package validation, game launch, level play, restart/exit, screenshot capture, and crash detection.

- [ ] **Step 5: Run all gates for the exact branch commit**

Run:
  - Python product-contract/parity tests;
  - host C++ renderer model tests;
  - Cppcheck;
  - Android64 Release build with minimum SDK 29;
  - cloud gameplay AutoTest against the exact build SHA.

Expected: all gates pass; AutoTest confirms basic stability only and is not treated as physical-device performance evidence.

- [ ] **Step 6: Physical-device smoke test**

Install the exact \`0.11.0-alpha\` artifact on the Huawei Y9s. Enter, restart, and exit levels containing multiple sprite layers, glow/detail sprites, groups, triggers, and animated objects. Confirm that appearance and gameplay remain vanilla because the experimental pipeline does not draw.

Record the tested artifact SHA and Geode log. Stop and revert the candidate if there are crashes, freezes, missing objects, or altered gameplay.

- [ ] **Step 7: Commit the parity gate**

Commit: \`test: validate parallel renderer snapshots and gameplay\`

## Completion Gate

This plan is complete only when Tasks 1–8 pass. The deliverable is a renderer-only \`0.11.0-alpha\` package with a read-only parallel pipeline, deterministic snapshots, conservative batching/geometry preparation, runtime GLES capability detection, vanilla-only visible rendering, and a working fallback path.

**Not included in this plan:** the first experimental draw replacement, migration of any object category, culling, compute-shader optimization, batching-performance claims, or a non-alpha optimization release. After this milestone is reviewed and physically validated, create a separate implementation plan for Stage 5 (one small static object category with an opt-in switch and automatic vanilla fallback). Only after renderer equivalence is established should a later plan cover optimization.

