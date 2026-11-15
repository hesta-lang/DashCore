# Android SSBO transfer benchmark (legacy synchronous reference)

## Purpose

This is the legacy synchronous Stage 4 reference after the successful tiny dynamic-SSBO correctness probe. It remains available for comparison, but its immediate readback can wait for GPU completion inside the calling gameplay update. Prefer the asynchronous Stage 5 test for ordinary validation. It measures a bounded CPU-to-GPU upload, compute submission, and GPU-to-CPU readback cycle at larger payload sizes. It is diagnostic only; it does not process Geometry Dash objects or draw anything.

## Settings and workload

The setting "Run legacy synchronous GLES SSBO benchmark" is disabled by default. It is automatically skipped when "Run asynchronous GLES SSBO benchmark" is enabled.

The benchmark uses two SSBOs:
- Input SSBO at binding 0, populated by CPU data through glBufferSubData.
- Output SSBO at binding 1, written by a deterministic GLES 3.1 compute shader and mapped read-only for CPU validation.

It tests 1 KiB, 4 KiB, 16 KiB, and 64 KiB per buffer. Each size is allocated once, then uploaded, dispatched, read back, and checked three times. The upper bound is 64 KiB per buffer (128 KiB for both buffers together). Four-wide workgroups are used. Every output value is validated before a size is marked complete.

## Measurements

For every payload size the log records:
- allocation_ms: CPU wall time around allocating and checking both buffer stores
- upload_average/minimum/maximum_ms: CPU wall time around buffer binding and glBufferSubData
- compute_submit_average/minimum/maximum_ms: CPU wall time around uniform setup, glDispatchCompute, and glMemoryBarrier submission
- readback_average/minimum/maximum_ms: CPU wall time around buffer binding, glMapBufferRange, output validation, and glUnmapBuffer
- cycle_average/minimum/maximum_ms: CPU wall time from the start of upload through completed readback validation

These are CPU-observed wall-clock timings, not isolated GPU timer-query measurements. In particular, compute_submit_ms measures command submission and barrier overhead; it must not be interpreted as actual GPU shader execution duration. Any GPU work that blocks the CPU can be reflected in the readback timing. No timer-query extension is required.

Expected complete workload counters:
- payload_sizes=4
- reallocations=8
- dispatches=12
- upload_bytes=261120
- readback_bytes=261120
- max_buffer_bytes=65536
- validated=true

## State and safety boundary

The probe is one-shot and opt-in. It saves and restores the current GL program, generic SSBO binding, and indexed SSBO bindings 0 and 1. It allocates only its two private buffers and its private compute program. It does not touch framebuffers, textures, vertex arrays, Cocos nodes, gameplay state, triggers, or draw commands. The original Cocos renderer remains the only visible draw path.

This test checks correctness and records timings for this specific device/driver/context. It is not an FPS benchmark, does not prove an optimization, and does not establish compatibility with level-object data. Compare repeated runs under similar device temperature and background load. Keep the existing tiny correctness probe separate from this larger timing test.


## Important

This legacy implementation maps its output immediately after dispatch. The map may stall until the GPU has completed the work. Do not enable it for the same run as the asynchronous benchmark; the mod suppresses this legacy path whenever the asynchronous setting is enabled. Use the separate asynchronous benchmark documentation for zero-timeout fence polling across frames.
