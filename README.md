# DashCore — Experimental Renderer for Android level objects

## 0.11.4-alpha — indexed capture and A/B diagnostic modes

This branch develops a separate rendering path for Geometry Dash level objects on Android64. This milestone adds bounded diagnostics to the read-only live snapshot stage; it does not change rendering.

- Adds a renderer-only Geode runtime, pure C++20 value model, validated sprite descriptor extractor, bounded value-only snapshot collector, stable z/traversal ordering, and contiguous batch planner keyed by layer, texture, and blend state.
- Rejects descriptors with unknown textures, unsupported blending, hidden sprites, invalid bounds, or non-finite transforms.
- Snapshot data owns values only; the collector rejects mixed level generations, duplicate sprite IDs, incomplete source reads, invalid attributes, and oversized snapshots. It never retains pointers to live Geometry Dash or Cocos nodes.
- Adds CPU quad-geometry preparation and an Android GLES buffer-upload backend with explicit capability detection. It preserves existing Cocos buffer bindings and does not expose a draw/present API.
- Adds a Geode adapter over GJBaseGameLayer::m_objects that builds a root-object index once per level. The full game-object array is scanned only when the array identity or element count changes; subsequent frames filter and traverse the cached PlayLayer descendants. Cocos child arrays are copied and traversed in z-order/order-of-arrival order without mutating the live tree.
- The snapshot path remains conservative: batched sprites and sprites with unsupported flags are skipped and counted separately from other validation failures. This build does not assume those common Geometry Dash sprite forms are errors, and it does not change the game's batch rendering.
- Measures the original PlayLayer::postUpdate(float) CPU wall-clock duration separately from the extra snapshot capture and source-collection durations. The **post-update time** measurement stops before the capture call begins.
- Records index rebuilds, array entries indexed, cached PlayLayer roots, objects excluded from the index, per-frame root/node/sprite counts, skipped batch/flag candidates, unique textures, snapshot vector capacity, and CPU timing distributions. Array-index counters are once-per-index-build; tree and sprite counters remain cumulative per captured frame.
- Keeps a fixed 512-sample timing window. Per-level averages cover the full session; p95 values cover the latest 512 captures. One summary is written when leaving the level; there is no per-frame logging.
- The snapshot_vector_capacity_bytes field estimates only the snapshot vector's element-buffer capacity. It is not total process memory and excludes allocator overhead, temporary child vectors, hash tables, Cocos objects, and GPU resources.
- The `capture-object-diagnostics` setting is **off by default**. With it off, DashCore sends only level lifecycle markers and performs no `m_objects` scan or snapshot collection; Launcher Diagnostics continues measuring frame cadence, callback CPU time, process memory, and thermal status. With it on, the cached object index and read-only per-frame capture are enabled from the next level.
- Capture timings are CPU wall-clock measurements. The post-update duration covers the original callback; capture and collection durations cover DashCore's work. They are not GPU timings or full-frame timings.

Geometry preparation and GLES buffer upload are not invoked by the game. This milestone does not submit GPU draw calls, replace any original draw call, or change visibility. Geometry Dash's vanilla rendering remains the only visible draw path. Menus, HUD, editor, player, effects, shaders, gameplay, physics, triggers, and input remain vanilla.

This diagnostic build is a measurement stage, not an optimization release. The A/B baseline is DashCore installed with `capture-object-diagnostics` off; the capture variant is the same setup with it on. Keep the same level, StartPos, duration, refresh rate, graphics settings, and background-app state for both runs. Compare `render_window` frame-interval and callback wall/CPU statistics plus process CPU/PSS; use `capture_avg_ms`, `collection_avg_ms`, and `index_build_total_ms` to quantify the added diagnostic work. No FPS improvement or performance claim is made.

## Build and validation

The Android64 workflow checks the product contract, compiles the standalone renderer model and diagnostics tests with C++20, runs Cppcheck, and builds the Geode package for Android64 with Android API 29 minimum.

Cloud gameplay automation and physical Huawei Y9s validation are separate gates. A cloud emulator result is not evidence of performance on Kirin 710F / Mali-G51 hardware.

## Launcher Diagnostics bridge

On Android, the experimental PlayLayer hooks send `onLevelStarted(null, null)` and `onLevelEnded()` lifecycle markers to `com.geode.launcher.LauncherDiagnostics` when that launcher component is present. The launcher remains responsible for recording frame cadence, Cocos callback wall time, GL-thread CPU time, process memory and thermal status. If the launcher class or method is unavailable, the bridge clears the lookup exception and leaves gameplay unchanged.

The bridge also forwards the completed `RendererDiagnosticSummary` as a structured JSON payload through `LauncherDiagnostics.onDashCoreLevelDiagnostics(String)`. Each level summary includes `capture_enabled`, `capture_skipped_frames`, index-build counters, and capture timings. This lets the exported JSONL identify its A/B variant without relying on the filename. These values are CPU-side diagnostics; they are not GPU draw-call counts, do not inspect/replace the visible draw path, and do not measure GPU execution time.
