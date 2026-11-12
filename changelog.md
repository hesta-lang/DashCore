## 0.11.0-alpha — read-only renderer foundation
- Replaces the old diagnostic/culling runtime with a dedicated experimental-renderer shell.
- Adds value-only renderer descriptors, sprite-attribute extraction, and a bounded snapshot collector with level-generation and duplicate-ID validation.
- Adds stable z/traversal ordering and contiguous batch planning by layer, texture, and blend state.
- Adds CPU quad-geometry preparation, runtime GLES version/capability parsing, and an Android VBO/EBO upload-only backend that restores Cocos buffer bindings.
- The source adapter remains test-only. These facilities are not connected to live gameplay and do not submit draw calls or replace any original rendering path.
- Vanilla rendering, gameplay, physics, effects, shaders, UI, and input remain unchanged. No performance gain is claimed.

