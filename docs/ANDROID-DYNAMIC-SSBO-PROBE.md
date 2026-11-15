# Android dynamic SSBO transfer probe

## Purpose

This is a second, opt-in validation stage after the one-shot GLES compute smoke test. It checks that DashCore can repeatedly resize shader-storage buffer objects, upload CPU-generated data, consume it in an OpenGL ES compute shader, and read back deterministic output.

The test is isolated from Geometry Dash level data. It does not collect objects, inspect triggers, replace shaders, issue draw calls, or render anything. The original Cocos renderer remains the only visible draw path.

## Test sequence

The setting named "Run GLES dynamic SSBO transfer probe" is disabled by default. When enabled, the first gameplay PlayLayer update runs the test once per process.

The probe uses two SSBOs:
- Input SSBO at binding 0, populated by the CPU with glBufferSubData.
- Output SSBO at binding 1, written by the compute shader and mapped read-only for validation.

It re-specifies both buffer stores at five payload sizes: 1, 4, 16, 64, and 256 uint32 values. This is a deliberately tiny maximum of 1 KiB per buffer. Each batch dispatches enough four-wide workgroups to cover its elements. CPU and GPU apply the same deterministic unsigned-integer transform, and every returned value is checked.

Expected successful log fields are:
- status=passed
- batches=5
- reallocations=10
- dispatches=5
- upload_bytes=1364
- readback_bytes=1364
- max_buffer_bytes=1024
- validated=true

The result is emitted as DashCore_GLES_DYNAMIC_SSBO_PROBE. Other statuses identify context/version/limit, shader, link, allocation, mapping, or output-validation failures. The counters report attempted uploads/dispatches and completed validated batches, not a performance benchmark.

## State and safety boundary

Before modifying GL state, the probe saves the current program, generic SSBO binding, and indexed SSBO bindings 0 and 1. It restores these bindings before deleting its own buffers/program. It does not touch framebuffers, textures, vertex arrays, Cocos scene nodes, or any draw state. It is one-shot and disabled unless the setting is explicitly enabled.

This proves only a small dynamic SSBO upload/compute/readback path on the active context. It does not prove that level-object payloads, trigger state, texture ordering, blending, or a complete object renderer are compatible. It makes no FPS or optimization claim.
