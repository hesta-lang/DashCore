# DashCore — Android Object Renderer Research

## 0.11.9-alpha — adaptive memory budget diagnostics

This validation version preserves the original Geometry Dash/Geode Cocos renderer as the only visible renderer and adds read-only process/system memory sampling.

- Samples Android/Linux `/proc/meminfo`, `/proc/self/status`, and `/proc/self/smaps_rollup`.
- Records system RAM total/available, process RSS/PSS, and process peak RSS when exposed by the kernel.
- Reserves a planning safety margin of at least 256 MiB or 10% of physical RAM, uses at most one eighth of remaining headroom, and caps DashCore's planned budget at 64 MiB.
- Caps a future individual buffer request at half the current budget and 16 MiB.
- Samples at level start and once per 600 frames, never performing procfs reads every frame.
- Fails closed when required system-memory data is unavailable or memory pressure is high.
- Does not allocate, reserve, lock, or pre-touch memory. The budget is diagnostic planning data only.
- Does not create GPU buffers, call GL/EGL, alter the EGL context, or change launcher initialization.
- Does not copy or link Bismuth source code or distribute a modified Bismuth binary.

## Memory-budget interpretation

`processPssBytes` is proportional memory attributed to the Geometry Dash process; `processRssBytes` and `processPeakRssBytes` describe resident memory. `systemAvailableBytes` is the kernel's `MemAvailable` estimate. These values do not reveal Android's exact per-process hard memory limit.

The planned memory budget is independent of the active GLES SSBO limit. A future backend must apply both limits and re-check the budget before every large allocation.

## Current implementation boundary

The monitor only reads procfs and calculates limits. It does not alter gameplay, editor, menus, triggers, shaders, physics, object rendering, or player rendering. Object capture remains disabled by default. No FPS or optimization claim is made.

The acceptance gate is an Android64 build and a device JSONL run. Compare the new `memory_budget_bytes`, PSS/RSS, and available-system-memory fields against launcher diagnostics before adding GPU-buffer allocation.
