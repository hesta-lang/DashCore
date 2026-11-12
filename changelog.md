## 0.11.0-alpha — read-only renderer foundation
- Replaces the old diagnostic/culling runtime with a dedicated experimental-renderer shell.
- Adds value-only renderer descriptors, pure sprite-attribute extraction, and a bounded snapshot collector with level-generation and duplicate-ID validation.
- Adds stable z/traversal ordering and a contiguous batch planner that preserves compatibility boundaries across texture and blend state.
- Source collection is still backed only by test sources; the live Geode adapter and GLES geometry/draw path are not enabled.
- Keeps all visible rendering on the original Geometry Dash renderer. No gameplay, physics, effects, shaders, UI, draw-order, or performance changes are claimed.

