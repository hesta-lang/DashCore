# DashCore Bismuth bridge

## Purpose

The bridge is a read-only compatibility boundary between Bismuth's object pipeline and the existing Geometry Dash/Cocos scene. It does not create a renderer and does not activate Bismuth draw submission.

## Exposed data

- **Level objects:** source-array index, original and normalized ZLayer, object draw order, parent mode/spritesheet, and base/detail blend mode.
- **Sprites:** the existing GameObject sprite tree, classified as base, detail, black, black-child or glow. The bridge emits the same high-level order used by Bismuth: glow, separate detail behind when applicable, base object tree, and separate detail in front when applicable. Within each sprite it traverses negative-z children, the sprite unless `getDontDraw()` is set, then non-negative-z children.
- **Transforms:** the accumulated Cocos affine transform copied at callback time.
- **Batch layers:** existing `parentForZLayer` batch nodes keyed by the supported ZLayer, sprite sheet and blend state, with the node's z-order and arrival order.
- **GLES capabilities:** the live current-context version and extensions, including compute, SSBO, indirect, multi-draw, SPIR-V and timer-query availability.

## Lifetime and safety

All GameObject, CCSprite and CCSpriteBatchNode pointers in the views are borrowed. They are valid only during the visitor callback and must not be retained or used asynchronously. The transform and classification metadata are copied values.

The bridge does not call `retain`/`release`, mutate GameObjects or Cocos nodes, change visibility, create or destroy batch nodes, reorder children, change GL bindings, compile shaders, upload buffers, issue draw calls, intercept Cocos visit/draw methods, or alter presentation.

Call object/sprite enumeration on the game logic thread at a safe point in the level lifecycle. Call the GLES capability query on the render thread while Cocos's existing EGL context is current. The query is read-only and must be repeated after context recreation.

## GLES 2 boundary

The launcher is restored to the original Geode GLES 2 setup. The bridge reports the actual current context rather than requesting a newer one. Under GLES 2, Bismuth's compute-shader, SSBO and indirect-draw paths are unavailable. The bridge must return those capabilities as false; it must not advertise desktop GL compatibility or create a second context. The vanilla Cocos renderer remains responsible for every visible draw.

This is a prerelease compatibility layer only. It does not claim a working Bismuth Android renderer, pixel parity, or a performance improvement.
