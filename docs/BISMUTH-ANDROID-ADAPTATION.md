# Bismuth-inspired Android rendering work

DashCore 0.8.0 adds an Android-side visibility-classification prototype, not a replacement renderer. The desktop Bismuth renderer depends on desktop OpenGL 4.3 behavior and a custom sprite/object pipeline; Android GLES 3.2 cannot be assumed to provide identical APIs or rendering semantics.

## 0.8.0 adapter stage

The diagnostic adapter compares two inventories: the PlayLayer Cocos child tree and the `PlayLayer::m_objects` array exposed by Geometry Dash's game-layer bindings. They are logged separately because the child tree alone may not enumerate the full level-object inventory. GPU visibility probe inputs and a second bounds comparison are built from the object array. Camera position and zoom are metadata only; no unverified camera transform is applied.

Safety policy:
- PlayerObject instances, including both player slots, are always force-visible.
- All trigger objects are force-visible.
- All Effect-class objects are force-visible.
- All grouped objects are force-visible until DashCore can reproduce and validate trigger-driven group transforms.
- Missing-parent, invalid-size, and non-finite bounds are force-visible.
- The GLES compute shader only produces a diagnostic visibility mask. It never changes a node's visibility or draw calls.

A CPU/GPU parity PASS validates only that the compute shader matches the CPU formula for the supplied input. It does not establish that the chosen bounds match the game's rendered geometry or that the scene collector covers every renderable object.

## Diagnostic overhead

The previous build scanned the scene graph every 30 draw frames. Version 0.8.0 removes those repeated scans and reuses the final 180-frame scene snapshot for the traversal comparison. The detailed bounds and geometry passes remain one-shot diagnostics after the sample completes; they are not run every frame.

## Why full Bismuth is not copied directly

Bismuth's desktop renderer uses desktop OpenGL entry points, shader behavior, indirect draw paths, and a custom batching pipeline. GLES 3.2 is not desktop OpenGL 4.3. Each shader, buffer layout, capability, sprite ordering rule, blend mode, group transform, and fallback path must be ported and verified independently.

## Next stages

1. Validate 0.8.0 world-space candidate bounds against actual visible object edges on device.
2. Compare PlayLayer::m_objects against the Cocos child tree and verify collector coverage against the full renderable scene, not just CPU/GPU formula parity.
3. Implement a read-only group-transform model before considering grouped objects for visibility filtering.
4. Prototype chunk-based visibility windows and prefetch only after world coordinates and player/camera movement are understood.
5. Investigate static sprite batching and draw-call reduction separately; preserve z-order, blending, color channels, pulses, glow/detail sprites, and child-node rendering.
6. Only after these stages, consider a guarded opt-in culling experiment. Triggers, effects, group-driven objects, collisions, portals, orbs, and editor behavior remain protected until individually validated.

## Test policy

Keep `experimental-render-culling=false`. The child-tree and object-array censuses are diagnostic evidence only; neither proves that every renderable sprite is safely represented. PlayerObject, triggers, effects, grouped objects, rotating objects, and invalid bounds remain protected. If any object appears late, disappears, flickers, or changes gameplay, stop the test and send the complete Geode log.
