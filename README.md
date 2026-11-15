# DashCore — Android Object Renderer Research

## 0.16.0-alpha — synthetic CPU object grouping validation

This validation version preserves the original Cocos renderer from Geometry Dash/Geode as the only visible renderer. It adds a bounded, opt-in CPU-only grouping diagnostic over synthetic records, read-only process memory-limit diagnostics, and four separate, opt-in GLES compute/SSBO tests. It does not inspect actual level objects, claim an FPS improvement, or activate an alternate object renderer.

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

### Stage 4 — legacy synchronous SSBO transfer timing benchmark

- The setting "Run legacy synchronous GLES SSBO benchmark" is disabled by default and runs once per process when manually enabled. It is retained only as a comparison with the asynchronous version and can stall the gameplay thread while reading output. It is automatically skipped whenever the asynchronous benchmark is enabled.
- It tests 1, 4, 16, and 64 KiB payloads per SSBO, with three upload/compute/readback/validation repetitions at each size.
- The largest allocation is 64 KiB per buffer (128 KiB total for the pair); it validates every output value before accepting a sample.
- It records allocation time plus average/minimum/maximum CPU wall-clock times for upload, Compute submission, readback, and the complete upload-to-readback cycle.
- The Compute measurement is command submission plus barrier overhead, not isolated GPU execution time. The readback measurement may include time spent waiting for GPU completion.
- Expected completion counters are four payload sizes, eight storage reallocations, twelve dispatches, and 261120 bytes uploaded and read back.
- It does not access level objects, triggers, shaders, sprites, or draw state. It is a bounded timing/correctness benchmark, not an FPS benchmark or an optimization claim.


### Stage 5 — frame-stepped asynchronous SSBO transfer benchmark

- The setting "Run asynchronous GLES SSBO benchmark" is disabled by default.
- It tests the same 1, 4, 16, and 64 KiB payloads, with three repetitions per size and the same 64 KiB-per-buffer upper bound.
- It advances only one bounded phase per gameplay update: allocate, upload/dispatch, or poll/readback. A single dispatch is submitted per cycle.
- After dispatch and the shader-storage/buffer-update barrier, it inserts a GLsync fence and flushes the command queue.
- Later frames call glClientWaitSync with flags=0 and timeout=0. GL_TIMEOUT_EXPIRED returns immediately and lets gameplay continue; there is no glFinish and no positive timeout.
- Output mapping and CPU validation happen only after the fence reports GL_ALREADY_SIGNALED or GL_CONDITION_SATISFIED. Each frame restores the GL program and generic/indexed SSBO bindings it observed at the start of that phase.
- It reports upload and Compute-submission CPU time, fence-observed completion latency, readback/validation time, end-to-end latency, and the number of poll frames. Fence latency includes frame polling granularity and is not isolated GPU execution time.
- The test uses the same expected counters as Stage 4: four payload sizes, eight storage reallocations, twelve dispatches, and 261120 bytes uploaded and read back.
- Shader compilation/linking still occurs once during initialization, and each payload size still requires two bounded glBufferData allocations. Those driver operations can take time; the asynchronous change specifically removes the CPU's immediate wait for GPU completion after each dispatch.
- It never accesses level objects, triggers, shaders, sprites, or Cocos draw state. It issues no draw calls and does not replace the original Cocos renderer.

### Stage 6 — synthetic CPU object grouping

- The setting "Run CPU object grouping diagnostic" is disabled by default and runs once when explicitly enabled during gameplay.
- The diagnostic creates exactly 1,024 synthetic records. It never reads `PlayLayer::m_objects`, Cocos nodes, sprites, triggers, player state, or any real level content.
- Each record contains a stable non-zero object identifier, X/Y position, rotation, X/Y scale, type identifier, and state flags.
- The CPU builder validates finite transforms, rejects zero or duplicate identifiers, enforces a maximum of 4,096 records, and creates structure-of-arrays buffers for position, rotation, scale, type, identifier, and state.
- Records are sorted by ascending type while preserving their original order within each type. A compact group-range table identifies each contiguous type range.
- The builder validates array lengths, group coverage, ordering, finite values, and identifier uniqueness before reporting success.
- Logs one `DashCore_CPU_OBJECT_GROUPING` summary containing input count, grouped count, group count, packed data bytes, group table bytes, elapsed CPU time, and validation status.
- This stage is CPU-only: no OpenGL calls, SSBO allocation, Compute dispatch, draw calls, Cocos integration, or gameplay-object changes.

## Memory-budget interpretation

processPssBytes is proportional memory attributed to the Geometry Dash process; RSS and peak RSS describe resident memory. systemAvailableBytes is the kernel's MemAvailable estimate. cgroup values represent the current memory controller hierarchy when discoverable. RLIMIT values are the process's soft and hard resource limits. These are distinct mechanisms and must not be conflated.

The existing planning budget remains based on system available memory and conservative headroom. A future renderer allocation must also consider finite cgroup headroom, driver-specific limits, GLES SSBO limits, and re-check memory immediately before allocation.

## Current implementation boundary

The original Geometry Dash/Geode Cocos renderer remains the only visible draw path. Object capture remains disabled by default. The CPU grouping diagnostic uses synthetic records only and is opt-in. The memory monitor only reads OS diagnostics. All four GLES probes are opt-in diagnostics; none is connected to object rendering or gameplay data.

DashCore does not copy, link, package, or distribute Bismuth source or a Bismuth-derived binary. The current branch is independent Android renderer research. No optimization or performance improvement is claimed.

The acceptance gate is an Android64 build, a controlled device test with all probes off first, followed by a separate run of the CPU synthetic grouping diagnostic and then separate runs with the tiny compute test, dynamic SSBO correctness test, and asynchronous SSBO benchmark enabled one at a time. Keep the legacy synchronous benchmark disabled unless a direct comparison is required. Compare the corresponding DashCore probe result in the Geode log before considering any further backend work.
