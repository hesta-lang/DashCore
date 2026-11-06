# Bismuth-inspired Android rendering work

DashCore does **not** embed Bismuth's renderer or copy its compiled Windows
binary. The uploaded `.geode` package contains a compiled Windows DLL and
shader resources, not the original C++ implementation. Bismuth's public source
is useful as an architectural reference, but its renderer is a full replacement
for Geometry Dash's object drawing path and is explicitly experimental.

## What DashCore adopts first

The first Android-safe step is conservative CPU-side visibility filtering at
the Cocos `CCNode::visit()` boundary:

- Skip only the visual visit of objects outside an expanded camera rectangle.
- Never cull Effect-class objects or objects for which `GameObject::isTrigger()`
  returns true.
- Permit grouped objects to be culled only when their current world-space
  center is farther than 1.25 times the larger screen dimension from player 1.
- If the player or either parent transform cannot be resolved, protect the
  grouped object (fail-safe).
- Do not modify object updates, collisions, trigger activation, group membership,
  or level data.
- Keep the option disabled by default until real-device testing confirms that
  objects do not pop in, disappear, or affect gameplay.

This is deliberately more conservative than Bismuth: Bismuth calculates group
transform state and object visibility as part of its own renderer. DashCore
does not yet own those systems.

## Why the full Bismuth compute renderer is not ported

The uploaded package metadata targets Windows only. Its renderer source checks
for desktop OpenGL 4.3 and loads desktop entry points such as compute dispatch,
shader-storage buffers, and indirect multi-draw. The included compute shader
also requests `GL_ARB_shading_language_include`. Android OpenGL ES 3.2 has
compute-shader capabilities, but that does not make desktop GLSL, desktop
extensions, buffer bindings, or indirect-draw entry points interchangeable.
A device-specific capability check and an ES shader rewrite would be required.

## Staged roadmap

1. **Grouped-aware CPU culling (0.6.2):** validate safety and count grouped
   objects protected near the player versus grouped objects eligible/cut far
   away. Culling remains opt-in.
2. **Measure before adding work:** compare the same level, same route, and same
   device with culling off/on. Keep frame-gap and callback timings separate;
   CPU duration around `drawScene()` is not a GPU timer.
3. **Static sprite batching research:** only prototype after inspecting sprite
   order, z-layers, texture sheets, blending, glow/detail sprites, and object
   child nodes. Incorrect batching can change visual order and break effects.
4. **Optional GLES compute experiment:** only if runtime checks confirm the
   required ES version and extensions. Start with a standalone capability test;
   do not replace vanilla rendering until output can be compared safely.
5. **Overdraw reduction:** investigate sprite meshes or conservative geometry
   simplification only after a GPU timing path exists. CPU culling alone cannot
   establish whether fragment overdraw is the bottleneck.

## Test protocol

- First launch with `experimental-render-culling=false`.
- Then enable it for a short test on a copy of a level or a safe practice run.
- Test grouped objects affected by move, rotate, scale, alpha, and toggle
  triggers, including objects that move from far away into the camera.
- Verify that triggers, particles/effects, collision, portals, orbs, and player
  interactions behave exactly as vanilla.
- Send the Geode log. Compare the `CULL FINAL` counters and frame-gap metrics.
  Disable the option immediately if any object appears late or disappears.

