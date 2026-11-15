# Android asynchronous SSBO transfer benchmark

## Purpose

This is Stage 5. It repeats the bounded transfer/correctness workload from the legacy timing test, but it does not immediately read GPU output after dispatch. A GLsync fence separates command submission from readback validation so the gameplay thread can continue while the GPU finishes.

It is a diagnostic only. It does not process Geometry Dash objects, draw anything, or replace the original Cocos renderer.

## Settings and workload

The setting "Run asynchronous GLES SSBO benchmark" is disabled by default. When enabled, it runs once in the current process and is cancelled if the gameplay level exits before completion.

The test uses two private SSBOs:
- Input at binding 0, populated by CPU data through glBufferSubData.
- Output at binding 1, written by a deterministic GLES 3.1 compute shader.

It tests 1 KiB, 4 KiB, 16 KiB, and 64 KiB per buffer. Each size is allocated once and processed for three repetitions. The maximum is 64 KiB per buffer (128 KiB for both buffers together). Every output value is checked against the deterministic CPU result.

## Frame-stepped synchronization

The state machine advances at most one bounded phase during a gameplay update:

1. Initialize the private compute program and two buffer objects.
2. Allocate and verify both buffer stores for the current payload size.
3. Upload one payload, dispatch one compute batch, issue the shader-storage/buffer-update memory barrier, create a GLsync fence, and call glFlush.
4. On later gameplay updates, poll the fence using glClientWaitSync(fence, 0, 0).
5. If the result is GL_TIMEOUT_EXPIRED, return immediately. Do not map the output and do not wait.
6. If the fence is GL_ALREADY_SIGNALED or GL_CONDITION_SATISFIED, map the output, validate every value, unmap, and advance to the next repetition or payload size.

There is no glFinish, no positive client-wait timeout, and no blocking loop that waits for a fence. Polling is bounded to 600 gameplay updates per dispatch; a fence that does not signal within that bound fails with fence_timeout. The probe cancels and releases its private objects when the level exits.

Each phase takes a fresh snapshot of the current GL program, generic SSBO binding, and indexed SSBO bindings 0 and 1, then restores those values before returning. It does not leave its SSBOs bound across frames.

## Measurements

For every payload size, the log records:
- allocation_ms: CPU wall time for both glBufferData calls and their size checks
- upload_avg_ms: CPU wall time around glBufferSubData
- compute_submit_avg_ms: CPU wall time for program/binding setup, dispatch, barrier, fence creation, and glFlush
- fence_latency_avg_ms/max_ms: time from queue flush until a later poll observes the fence signaled; this includes polling cadence and frame scheduling and is not a GPU timer measurement
- fence_poll_frames_avg/max: number of gameplay updates spent polling each dispatch
- readback_avg_ms: time for mapping, CPU validation, and unmapping after the fence signaled
- end_to_end_avg_ms: CPU-observed wall time from upload start until readback validation completes

Expected complete workload counters:
- payload_sizes=4
- reallocations=8
- dispatches=12
- upload_bytes=261120
- readback_bytes=261120
- max_buffer_bytes=65536
- validated=true

## Remaining synchronous driver work

This test removes the immediate CPU wait for GPU completion after each dispatch, but it does not make every driver call asynchronous. Shader compilation/linking occurs once during initialization, and each payload size has two bounded glBufferData allocations. Those calls can still take time on some drivers. The map and validation step is not reached until the fence has signaled.

The legacy "Run legacy synchronous GLES SSBO benchmark" setting is automatically skipped while this asynchronous setting is enabled. Keep the legacy setting disabled during ordinary tests.

## Scope and safety boundary

The benchmark allocates only its two private buffers and its private compute program. It does not touch framebuffers, textures, vertex arrays, Cocos nodes, gameplay state, triggers, sprites, or draw commands. It does not use timer-query extensions. The original Geometry Dash/Geode Cocos renderer remains the only visible draw path.

This test verifies bounded SSBO transfer correctness and characterizes CPU-observed completion behavior on a particular device/driver/context. It is not an FPS benchmark, does not prove an optimization, and does not establish compatibility with actual level-object data.
