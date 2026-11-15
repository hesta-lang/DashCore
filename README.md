# DashCore — Android Object Renderer Research

## 0.12.0-alpha — memory-limit visibility and isolated GLES compute probe

This validation version preserves the original Cocos renderer from Geometry Dash/Geode as the only visible renderer. It adds read-only process memory-limit diagnostics and a separate, opt-in compute-shader smoke test.

### Stage 1 — Android process memory limits

- Reads /proc/meminfo, /proc/self/status, and /proc/self/smaps_rollup.
- Reads cgroup v2 memory.max/memory.current and cgroup v1 memory.limit_in_bytes/memory.usage_in_bytes when the current process membership and mount are readable.
- Records whether a cgroup limit is finite or unlimited; missing/inaccessible files are reported as unavailable, not guessed.
- Records process VmSize, RSS, PSS, and peak RSS.
- Queries soft and hard RLIMIT_AS and RLIMIT_RSS values when the platform exposes them.
- Samples at level start and once per 600 frames; no per-frame procfs/cgroup reads are added.
- Does not change the planning budget based on RLIMIT values. Linux may not enforce RLIMIT_RSS, and missing cgroup visibility is not proof that no Android memory limit exists.
- Does not reserve, allocate, lock, or pre-touch memory as part of the memory diagnostics.

### Stage 2 — isolated GLES 3.1 compute self-test

- The setting "Run GLES compute diagnostic probe" is disabled by default.
- When manually enabled, the first gameplay PlayLayer::postUpdate attempts exactly one small compute test.
- The test requires a current OpenGL ES 3.1-or-newer context, compiles a private compute shader, dispatches four invocations, and verifies four 32-bit output values in a 16-byte SSBO.
- It restores the previous GL program and generic/indexed shader-storage-buffer bindings after the test.
- It does not read or change level objects, sprites, triggers, player state, shaders, Cocos nodes, or draw commands. It issues no draw calls and does not replace the Cocos renderer.
- A result is written to the log as DashCore_GLES_COMPUTE_PROBE. Possible outcomes include no current context, unsupported GLES version, shader/link failure, buffer/readback failure, or passed.
- This is a capability and readback smoke test only. It is not an object renderer, does not prove visual compatibility, and makes no FPS or optimization claim.

## Memory-budget interpretation

processPssBytes is proportional memory attributed to the Geometry Dash process; RSS and peak RSS describe resident memory. systemAvailableBytes is the kernel's MemAvailable estimate. cgroup values represent the current memory controller hierarchy when discoverable. RLIMIT values are the process's soft and hard resource limits. These are distinct mechanisms and must not be conflated.

The existing planning budget remains based on system available memory and conservative headroom. A future renderer allocation must also consider finite cgroup headroom, driver-specific limits, GLES SSBO limits, and re-check memory immediately before allocation.

## Current implementation boundary

The original Geometry Dash/Geode Cocos renderer remains the only visible draw path. Object capture remains disabled by default. The memory monitor only reads OS diagnostics. The compute probe is a one-shot opt-in diagnostic with a 16-byte buffer; it is not connected to object rendering or gameplay data.

DashCore does not copy, link, package, or distribute Bismuth source or a Bismuth-derived binary. The current branch is independent Android renderer research. No optimization or performance improvement is claimed.

The acceptance gate is an Android64 build, a controlled device test with the compute setting off first, and then one run with it enabled. Compare cgroup/RLIMIT fields and the DashCore_GLES_COMPUTE_PROBE result in the resulting logs before considering any further backend work.
