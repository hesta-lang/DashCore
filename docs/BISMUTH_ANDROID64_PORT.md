# Bismuth Android64 port plan

## Status: platform foundation only (0.11.5-alpha)

This document records the concrete Android64 port constraints identified from the upstream Bismuth source and the November 14, 2026 Huawei STK-LX3 diagnostic capture. It is a staged engineering plan, not a claim that Bismuth has been ported or that performance has improved.

The new GLES capability parser enumerates indexed extensions on GLES 3.x with `glGetStringi` resolved through EGL. It reports the context version, extension-enumeration status, compute shaders, shader storage buffers, single-command indirect drawing, multi-draw indirect, shader draw parameters, SPIR-V shader loading, and disjoint timer queries independently. A missing optional capability must select a fallback or disable the corresponding experiment; it must never be inferred from the GLES version alone unless it is core to that version.

## Device profile measured

The uploaded launcher diagnostic identifies an OpenGL ES 3.2 context with GLSL ES 3.20 on an ARM Mali-G51. The reported feature profile is:

- Compute shaders and shader storage buffers: supported.
- Single-command indirect drawing: supported by the GLES 3.1+ core path.
- Multi-draw indirect: not reported as supported.
- Shader draw parameters: not reported as supported.
- `GL_EXT_disjoint_timer_query`: reported as supported.
- The display reports 60 Hz. Renderer callbacks are not the same metric as physical panel presentation.

The runtime extension enumerator added in 0.11.5-alpha will record the actual extension list exposed by the current context when the GLES backend is initialized. The captured device report is a baseline; runtime capability detection remains authoritative for a running context.

## Upstream dependencies that cannot be carried over unchanged

The audited upstream code uses Windows-specific WGL entry points and OpenCL/OpenGL interop (`wglGetCurrentContext`, `wglGetCurrentDC`, `LoadLibraryA("OpenCL.dll")`, and `GetProcAddress`). These APIs cannot be reused in the Android renderer. The upstream OpenCL helper is used to estimate a GPU compute-unit/core count for work-group sizing; Android should replace this with a bounded GLES workload heuristic or a small measured tuning table. Do not make assumptions about Mali core count from OpenCL.

The upstream GL loader requests desktop functions such as `glMultiDrawElementsIndirect`, `glMultiDrawArraysIndirect`, `glSpecializeShader`, and desktop program-resource APIs. The upstream shader path targets desktop GLSL 450/460 and SPIR-V specialization. It must be replaced with GLSL ES 3.20 source compiled by the Android GLES driver, with explicit layouts and bindings compatible with the device. SPIR-V must remain optional and must not be a requirement for the Android path.

## Required Android substitutions

1. **Context and function loading:** use the current EGL/GLES context and resolve optional entry points through `eglGetProcAddress`. Every resolved pointer must be checked before use. Re-query after context recreation.
2. **Shader compilation:** use GLSL ES 3.20 for compute and graphics stages. Remove desktop-only directives and SPIR-V specialization assumptions; validate every shader on the target driver.
3. **Compute data path:** retain compute shaders and SSBO only when the live capability profile confirms them. Respect `GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS`, `GL_MAX_SHADER_STORAGE_BLOCK_SIZE`, and `GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS` reported by the actual context.
4. **Draw command submission:** GLES single-command indirect drawing is not the same as multi-draw indirect. The first fallback should submit one indirect command per draw/layer where supported. Measure CPU overhead; a large number of individual indirect calls may erase the gains. A direct-draw fallback must remain available.
5. **Shader IDs and work groups:** do not rely on shader draw parameters. Pass object/sprite indices using SSBO data and built-in work-group/global invocation IDs, with explicit bounds checks.
6. **GPU profiling:** use disjoint timer-query results only after availability is reported. Discard disjoint samples and never block waiting for a query result.

## Correct sprite extraction and render semantics

The current DashCore experimental adapter is deliberately conservative and its latest capture accepted zero sprites. It traverses generic Cocos nodes, while upstream Bismuth extracts each GameObject into its constituent base, detail, black, black-child, and glow sprites. The Android adapter therefore still needs a dedicated GameObject/sprite extractor that reproduces the upstream order: glow sprite, color sprite relative to the object according to its layering flag, object sprite tree, and child traversal in negative-z / parent / non-negative-z order.

Each extracted sprite must carry enough immutable data to reproduce vanilla output: owner object, sprite type, spritesheet identity, texture/UV rectangle, blend factors, z-layer and original traversal order, per-vertex color and alpha, local transform, flips/rotated atlas handling, group combination, object color channels, opacity, and applicable special effects. Any case not modeled exactly must be rejected and left to vanilla rendering; never approximate it.

The final render sequence must preserve the effective vanilla order across ZLayer, spritesheet, blend state, and within-batch order. The existing Cocos renderer stays visible until the replacement has passed image comparison and gameplay tests. Do not intercept `CCNode::visit`, `CCSprite::draw`, `CCSpriteBatchNode::draw`, or the present/swap path as part of this preparation milestone.

## Validation gates before enabling rendering

- **Gate A — capability tests:** host tests cover GLES core-version rules, exact extension-name matching, and missing optional features; Android64 CI builds the mod.
- **Gate B — extraction parity:** compare object/sprite counts and ordered descriptors for simple, decorated, glow, blended, flipped, animated, and trigger-driven objects. Unsupported cases must be counted and remain vanilla.
- **Gate C — offscreen parity:** render experimental output offscreen and compare pixels against vanilla on curated levels. Include alpha blending, ZLayer changes, color/detail channels, pulses, glow, invisible blocks, portals, particles, and checkpoints.
- **Gate D — performance:** compare CPU wall/CPU-thread time and GPU execution time on the same physical device, level, StartPos, graphics settings, refresh rate, and thermal state. Report p50/p95/max and dropped-frame intervals, not callback count as panel FPS.
- **Gate E — controlled activation:** only after parity and stability tests should a feature flag permit experimental level-object drawing. Menus, HUD, player, editor, effects, gameplay, physics, triggers, and input remain vanilla.

No Bismuth rendering path or new draw submission is enabled by this milestone. No optimization or visible rendering replacement is activated by this milestone.
