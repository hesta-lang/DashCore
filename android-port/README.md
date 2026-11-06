# Bismuth Android port (staged, not linked to DashCore yet)

This directory is the isolated Android adaptation area for the Bismuth renderer.
It is deliberately **not included in DashCore's CMake target yet**. The port
must pass its Android capability and correctness stages before any renderer
hook is connected to gameplay.

## Why the desktop renderer cannot be copied directly

The supplied Bismuth source is a desktop OpenGL 4.3 renderer. Its current
implementation relies on compute shaders and shader-storage buffers,
multi-draw indirect entry points, desktop shader include / specialization
paths, desktop GL function loading through Windows APIs, and a custom object
and sprite batching renderer that reproduces group transforms, color channels,
pulses, blending, special sprites, and object ordering.

Android OpenGL ES 3.2 is not equivalent to desktop OpenGL 4.3. A device
reporting GLES 3.2 does **not** guarantee multi-draw indirect, desktop shader
include, SPIR-V specialization, or the complete Bismuth feature set.

## Port stages

1. **Capability probe:** query the active Android GL context and exact
   extensions. Never load Windows GL entry points or assume a feature from
   the GLES version string alone.
2. **GLES shader conversion:** convert desktop GLSL to explicit GLES 3.x
   shader versions, replace desktop-only include/specialization behavior with
   a deterministic source preprocessor, and validate precision/layout rules.
3. **CPU/GLES fallback renderer:** implement a conservative path using APIs
   guaranteed by the minimum supported GLES version. Keep the original
   Geometry Dash renderer available as a fallback.
4. **Optional compute path:** enable compute culling only if GLES 3.1+
   compute and SSBO support are present. Do not make it a requirement.
5. **Optional indirect draw path:** enable only after exact extension/function
   checks and device tests. Otherwise use ordinary GLES draw calls.
6. **DashCore integration:** connect the validated adapter behind an opt-in
   setting, compare identical levels/settings with culling off/on, and keep
   triggers, effects, collisions, group actions, checkpoints, and editor
   behavior on the original game path until individually validated.

## Current implementation

The header and source in this directory provide an isolated runtime
capability probe. It reports GLES version, vendor, renderer, and relevant
extension support. The full Bismuth pipeline is intentionally reported
unsupported on GLES; compute and timer-query support are reported separately.

This is only the first adaptation layer. It is not yet a renderer, does not
change game rendering, and must not be wired into DashCore until the shader
and fallback stages are complete.

## Upstream and license

The source adaptation is based on the user-provided Bismuth repository archive.
Bismuth is distributed under GNU GPL version 3 or later; preserve upstream
copyright/license notices in any derived source files and include the full
license text before redistributing a derived renderer.
Upstream: https://github.com/IliasHDZ/Bismuth
