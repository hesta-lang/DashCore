# DashCore — Android Object Renderer Research

## 0.14.0-alpha — memory diagnostics and bounded GLES SSBO transfer benchmark

This validation version preserves the original Cocos renderer from Geometry Dash/Geode as the only visible renderer. It adds read-only process memory-limit diagnostics and three separate, opt-in GLES compute/SSBO tests. It does not claim an FPS improvement or activate an alternate object renderer.

### Stage 1 — Android process memory limits

- Reads /proc/meminfo, /proc/self/status, and /proc/self/smaps_rollup.
- Reads cgroup v2 memory.max/memory.current and cgroup v1 memory.limit_in_bytes/memory.usage_in_bytes when the current process membership and mount are readable.
- Records whether a cgroup limit is finite or unlimited; missing/inaccessible files are reported as unavailable, not guessed.
- Records process VmSize, RSS, PSS, and peak RSS.
- Queries soft and hard RLIMIT_AS and RLIMIT_RSS values when the platform exposes them.
- Samples at level start and once per 600 frames; no per-frame procfs/cgroup reads are added.
- Does not reserve, allocate, lock, or pre-touch memory as part of the memory diagnostics.

### Stage 2 — isolated GLES 3.1 compute smoke test

- The setting "Run GLES compute diagnostic probe" is disabled by default.
- When manually enabled, the first gameplay PlayLayer::postUpdate attempts exactly one small compute test.
- The test requires a current OpenGL ES 3.1-or-newer context, compiles a private compute shader, dispatches four invocations, and verifies four 32-bit output values in a 16-byte SSBO.
- It restores the previous GL program and generic/indexed shader-storage-buffer bindings after the test.
- It does not read or change level objects, sprites, triggers, player state, shaders, Cocos nodes, or draw commands. It issues no draw calls and does not replace the Cocos renderer.
- A result is written to the log as DashCore_GLES_COMPUTE_PROBE.
- This is a capability and readback smoke test only, not an object renderer.

### Stage 3 — dynamic SSBO CPU-to-GPU transfer

- The setting "Run GLES dynamic SSBO transfer probe" is disabled by default and runs once per process when manually enabled.
- It uses two SSBOs: CPU-written input at binding 0 and GPU-written output at binding 1.
- The same buffer objects are reallocated at five sizes: 1, 4, 16, 64, and 256 uint32 values (4 bytes through 1 KiB per buffer).
- Each pass uploads CPU-generated input with glBufferSubData, dispatches a four-wide compute shader, then maps and validates every output value on the CPU.
- It records validated batches, storage reallocations, dispatches, upload/readback byte counts, maximum buffer size, and the result status.
- It restores the previous GL program, generic SSBO binding, and indexed bindings 0 and 1 before deleting its resources.
- The result is written as DashCore_GLES_DYNAMIC_SSBO_PROBE. Expected success is five validated batches, ten storage reallocations, five dispatches, and 1364 bytes uploaded and read back.
- It does not access level objects or draw anything. It is a tiny transfer correctness test, not a throughput benchmark or optimization.

## Memory-budget interpretation

processPssBytes is proportional memory attributed to the Geometry Dash process; RSS and peak RSS describe resident memory. systemAvailableBytes is the kernel's MemAvailable estimate. cgroup values represent the current memory controller hierarchy when discoverable. RLIMIT values are the process's soft and hard resource limits. These are distinct mechanisms and must not be conflated.

The existing planning budget remains based on system available memory and conservative headroom. A future renderer allocation must also consider finite cgroup headroom, driver-specific limits, GLES SSBO limits, and re-check memory immediately before allocation.

## Current implementation boundary

The original Geometry Dash/Geode Cocos renderer remains the only visible draw path. Object capture remains disabled by default. The memory monitor only reads OS diagnostics. All three GLES probes are opt-in, one-shot diagnostics; neither is connected to object rendering or gameplay data.

DashCore does not copy, link, package, or distribute Bismuth source or a Bismuth-derived binary. The current branch is independent Android renderer research. No optimization or performance improvement is claimed.

The acceptance gate is an Android64 build, a controlled device test with all probes off first, followed by separate runs with the tiny compute test, dynamic SSBO correctness test, and larger SSBO timing benchmark enabled one at a time. Compare the corresponding DashCore probe result in the Geode log before considering any further backend work.
