# DashCore — Experimental Renderer for Android level objects

## 0.11.0-alpha — read-only renderer foundation

This branch develops a separate rendering path for Geometry Dash level objects on Android64.

- Adds a renderer-only Geode runtime, pure C++20 value model, validated sprite descriptor extractor, bounded value-only snapshot collector, stable z/traversal ordering, and contiguous batch planner keyed by layer, texture, and blend state.
- Rejects descriptors with unknown textures, unsupported blending, hidden sprites, invalid bounds, or non-finite transforms.
- Snapshot data owns values only; the collector rejects mixed level generations, duplicate sprite IDs, incomplete source reads, invalid attributes, and oversized snapshots. It never retains pointers to live Geometry Dash or Cocos nodes.
- Adds CPU quad-geometry preparation and an Android GLES buffer-upload backend with explicit capability detection. It preserves existing Cocos buffer bindings and does not expose a draw/present API.
- The `LevelObjectSource` adapter is still an interface tested with fake sources; no live `GameObject`/`CCNode` extraction or gameplay integration is connected yet. Geometry preparation and buffer upload are not invoked by the game. This milestone does **not** submit GPU draw calls, replace any original draw call, or change visibility.
- Geometry Dash's vanilla rendering remains the only visible draw path. Menus, HUD, editor, player, effects, shaders, gameplay, physics, triggers, and input remain vanilla.

The goal is to build and validate a parallel object-rendering pipeline in small stages before any category is considered for replacement. No FPS improvement or performance claim is made by this read-only foundation.

## Build and validation

The Android64 workflow checks the product contract, compiles the standalone renderer model tests with C++20, runs Cppcheck, and builds the Geode package for Android64 with Android API 29 minimum.

Cloud gameplay automation and physical Huawei Y9s validation are separate gates. A cloud emulator result is not evidence of performance on Kirin 710F / Mali-G51 hardware.
