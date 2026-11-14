# DashCore — Experimental Renderer for Android level objects

## 0.11.1-alpha — bounded capture diagnostics

This branch develops a separate rendering path for Geometry Dash level objects on Android64. This milestone adds bounded diagnostics to the read-only live snapshot stage; it does not change rendering.

- Adds a renderer-only Geode runtime, pure C++20 value model, validated sprite descriptor extractor, bounded value-only snapshot collector, stable z/traversal ordering, and contiguous batch planner keyed by layer, texture, and blend state.
- Rejects descriptors with unknown textures, unsupported blending, hidden sprites, invalid bounds, or non-finite transforms.
- Snapshot data owns values only; the collector rejects mixed level generations, duplicate sprite IDs, incomplete source reads, invalid attributes, and oversized snapshots. It never retains pointers to live Geometry Dash or Cocos nodes.
- Adds CPU quad-geometry preparation and an Android GLES buffer-upload backend with explicit capability detection. It preserves existing Cocos buffer bindings and does not expose a draw/present API.
- Adds a synchronous Geode adapter over GJBaseGameLayer::m_objects and the main m_objectLayer. It traverses copied Cocos child arrays in z-order/order-of-arrival order without sorting or mutating the live tree.
- Captures only unbatched sprites with supported blend factors, unrotated/unflipped atlas UVs, no vertex mods, zero vertex depth, rectangular axis-aligned quads, and uniform per-vertex color. Unsupported sprites and objects outside the main object layer remain on vanilla rendering.
- Measures the original PlayLayer::postUpdate(float) CPU wall-clock duration separately from the extra snapshot capture and source-collection durations. The original update measurement stops before the capture call begins.
- Records node visits, root objects, candidate/accepted sprites, rejection reasons, unique textures, snapshot vector capacity, successful/failed captures, and CPU timing distributions.
- Keeps a fixed 512-sample timing window. Per-level averages cover the full session; p95 values cover the latest 512 captures. One summary is written when leaving the level; there is no per-frame logging.
- The snapshot_vector_capacity_bytes field estimates only the snapshot vector's element-buffer capacity. It is not total process memory and excludes allocator overhead, temporary child vectors, hash tables, Cocos objects, and GPU resources.
- Aggregated node/object/sprite counters are cumulative across captured frames. The same level object is counted again on every frame; these totals are work counters, not unique-level-object counts.
- Capture timings are CPU wall-clock measurements. The post-update duration covers the original callback; capture and collection durations cover DashCore's work. They are not GPU timings or full-frame timings.

Geometry preparation and GLES buffer upload are not invoked by the game. This milestone does not submit GPU draw calls, replace any original draw call, or change visibility. Geometry Dash's vanilla rendering remains the only visible draw path. Menus, HUD, editor, player, effects, shaders, gameplay, physics, triggers, and input remain vanilla.

This diagnostic build is a measurement stage, not an optimization release. Capturing still runs synchronously after PlayLayer::postUpdate(float), so it can add CPU work. No FPS improvement or performance claim is made.

## Build and validation

The Android64 workflow checks the product contract, compiles the standalone renderer model and diagnostics tests with C++20, runs Cppcheck, and builds the Geode package for Android64 with Android API 29 minimum.

Cloud gameplay automation and physical Huawei Y9s validation are separate gates. A cloud emulator result is not evidence of performance on Kirin 710F / Mali-G51 hardware.
