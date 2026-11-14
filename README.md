# DashCore — Android Object Renderer Research

## 0.11.8-alpha — clean-room Android backend groundwork

This source-only branch prepares an independent Android object-rendering path while preserving the original Geometry Dash/Geode Cocos renderer as the only visible renderer.

- Adds a pure OpenGL ES capability model for the active context version, exact extension tokens, and compute limits.
- Selects a conservative path: vanilla fallback, CPU-prepared vertex/index buffers, compute with single indirect draws, or compute with multi-draw indirect when its extension is actually advertised.
- Does not call GL/EGL, allocate GPU resources, submit draw commands, alter the EGL context, or change launcher initialization.
- Keeps the existing object collector, geometry model, and backend interface separate from platform API calls.
- Does not copy or link Bismuth source code. The Android implementation is independent and uses public API specifications as its platform contract.
- Does not package or distribute a modified Bismuth binary. This branch is source research only; it does not produce a .geode artifact.

The original Cocos renderer remains the only visible renderer. All capture diagnostics remain disabled by default. No triggers, shader effects, physics, gameplay state, menus, editor, or player rendering are replaced.

## Current implementation boundary

GlesCapabilities is a pure decision model. A future context probe must supply values from the real current GLES context on the render thread. Until that probe is wired and verified on hardware, the capability result is not evidence that a device exposes a feature.

The preferred path is conservative:

- Unknown or pre-ES 2.0 context: vanilla fallback.
- ES 2.0 or ES 3.0: CPU-prepared VBO/IBO geometry.
- ES 3.1+: compute shaders, SSBOs, and single indirect drawing are eligible only when the queried storage/work-group limits are non-zero.
- ES 3.1+ with GL_EXT_multi_draw_indirect: multi-draw indirect is eligible; otherwise use single indirect calls.
- GL_EXT_disjoint_timer_query is optional and is not required for rendering.

## Source-only tests

The DashCore Android Renderer Source Tests workflow compiles the CPU model and capability selector, runs the contract tests, and checks portability. It does not build, sign, upload, or publish a Geode package.

This is not an optimization release and makes no FPS or visual-parity claim. The next acceptance gate is to inspect the actual context version, extension list, and limits on the Huawei device without changing the renderer or launcher.
