# Android GLES backend adaptation — source-only phase

## Status and boundary

This branch is an independent implementation in DashCore. It does not copy, include, link, or patch Bismuth source files. Bismuth's public technical explanation is used only to understand the workload at a behavioral level: sprite/object collection, layer ordering, texture/blend grouping, dynamic color and trigger state, GPU-side processing, and the final object draw stage.

No modified Bismuth binary or derived .geode package is produced here. The upstream repository contains additional distribution terms in its LICENSE; written permission from its author has not been obtained, so this work does not package or distribute a modified Bismuth program.

The original Geometry Dash/Geode Cocos renderer remains the only visible draw path. No draw hooks, EGL changes, GL resource operations, trigger substitutions, or shader replacements are enabled in this phase.

## Platform mapping

| Upstream desktop assumption | Android source strategy |
| --- | --- |
| Windows wglGetProcAddress loader | Do not carry WGL loading. Core GLES entry points must match the active GLES headers/runtime; optional extension procedures are resolved only after exact extension checks. |
| OpenCL device discovery and GL sharing | Remove the OpenCL requirement. GLES compute, when supported by the active context, is the optional compute path. No OpenCL/GL interop is assumed. |
| Desktop compute + shader-storage buffers | GLES 3.1 core capability, gated by the actual context version and queried implementation limits. |
| Desktop glSpecializeShader / SPIR-V specialization | Use GLSL ES source shaders (#version 300 es, 310 es, or 320 es as appropriate). Do not make desktop SPIR-V specialization a requirement. |
| glMultiDraw*Indirect assumed present | Treat GL_EXT_multi_draw_indirect as optional. Without it, use one GLES 3.1 indirect draw per command or the CPU-prepared VBO/IBO path. |
| Desktop GPU timer queries | Treat GL_EXT_disjoint_timer_query as optional measurement support, never as a startup requirement. |

Khronos specifies compute shaders and shader-storage blocks as OpenGL ES 3.1 functionality. The ES 3.1 API also includes single indirect draw commands. Multi-draw indirect is a separate extension whose specification explicitly requires OpenGL ES 3.1. These are different capability gates, not one general “GLES 3.2 compatible” switch.

## First source implementation

src/renderer/core/GlesCapabilities.hpp/.cpp contains a platform-neutral capability resolver:

- Parses only strings identifying an OpenGL ES context.
- Recognizes the reported major/minor version, including common OpenGL ES-CM version formatting.
- Matches extension names as complete whitespace-separated tokens.
- Enables compute/SSBO/single-indirect eligibility from ES 3.1 or newer.
- Enables multi-draw indirect only when GL_EXT_multi_draw_indirect is reported.
- Treats GL_EXT_disjoint_timer_query as optional.
- Requires non-zero queried storage and compute-workgroup limits before selecting a compute path.
- Selects CPU VBO/IBO preparation when the context supports GLES 2.0+ but compute requirements are missing.
- Selects vanilla fallback for an unknown or pre-ES 2.0 context.
- Explicitly keeps OpenCL interop and desktop SPIR-V specialization out of the Android contract.

This file makes no GL/EGL calls. It does not prove the context on the phone is ES 3.1/3.2 and does not activate a renderer. A real context probe must later populate GlesContextInfo on the render thread and record GL_VERSION, the exact extension set, and the queried limits.

## Implementation sequence

1. **Capability observation only (current stage).** Read the active context without changing EGL setup or draw behavior. Confirm whether the currently-created Cocos context is ES 2.0, ES 3.0, ES 3.1, or ES 3.2. Record the actual extension tokens and relevant limits.
2. **CPU geometry backend.** Add GLES resource management for VBO/IBO and ordinary glDrawElements calls, initially with drawing disabled. Keep it isolated from Geode hooks and use the vanilla renderer as fallback.
3. **Parallel comparison.** Prepare geometry and batches without submitting them to the game's framebuffer. Compare ordering, UVs, blend state, color, alpha, transforms, and sprite coverage against the Cocos state.
4. **Optional compute path.** Only on ES 3.1+ with valid SSBO/workgroup limits, introduce compute shaders using GLSL ES source. Keep single indirect draw as the baseline.
5. **Optional multi-draw path.** Resolve the EXT entry points only when GL_EXT_multi_draw_indirect is present and verify the relevant VAO/buffer preconditions. If absent, retain single indirect or CPU drawing.
6. **Very limited object replacement.** Consider one static sprite category only after comparison and fallback tests pass. Dynamic triggers, groups, pulses, shader effects, particles, the editor, UI, and player rendering stay vanilla until individually proven equivalent.

## Safety conditions

- The active Cocos EGL context is not changed in this phase.
- Never issue GLES calls from a thread without the intended context current.
- Re-query capabilities after context recreation; do not retain function pointers across a lost context without revalidation.
- Never treat the GPU name or Android API level as proof of a GLES feature.
- Unknown extension, invalid limit, incomplete sprite data, unsupported blend state, or mismatch means vanilla fallback.
- The renderer must never draw the same object through both paths in one frame.
- Do not claim a performance improvement until repeated physical-device measurements show one.

## References

- Bismuth technical explanation: https://github.com/iliasHDZ/Bismuth/blob/master/TechnicalExplanation.md — behavioral overview only; no code is imported.
- OpenGL ES 3.1 announcement: https://www.khronos.org/news/press/khronos-releases-opengl-es-3.1-specification — compute shaders and indirect drawing.
- Khronos OpenGL ES 3.1 specification: https://registry.khronos.org/OpenGL/specs/es/3.1/es_spec_3.1.pdf — core API and shader-storage behavior.
- GL_EXT_multi_draw_indirect specification: https://registry.khronos.org/OpenGL/extensions/EXT/EXT_multi_draw_indirect.txt — separate ES extension; requires ES 3.1.
- Khronos OpenGL ES registry: https://registry.khronos.org/OpenGL/index_es.php — current ES core and extension registry.
