# DashCore — Android rendering optimization

## 0.10.6-alpha — GameObject visual-hierarchy diagnostics

- Extends the opt-in render-pipeline profile with a one-shot read-only inventory of every usable entry in `PlayLayer::m_objects` after the 180-frame render sample.
- Traverses each GameObject's Cocos child subtree and records direct children, descendant nodes, descendant `CCSprite` instances, nested GameObjects, batch-node ancestors, and subtree depth.
- Reports five subtree-size buckets and up to eight objects with the largest child hierarchies to identify where sprite/node fan-out is concentrated.
- Applies per-object (4,096 nodes), per-object depth (64), and total (250,000 nodes) safety caps; reports truncation and scan duration.
- Per-root descendant counts can overlap when GameObjects are nested. A `CCSpriteBatchNode` ancestor is a hierarchy clue, not proof of actual atlas membership.
- The scan is read-only and runs once after the render sample. It does not alter node visibility, transforms, draw order, batching, gameplay, or UI.
- Performance Diagnostics and experimental static culling remain separately controlled; culling remains OFF by default.
- This remains diagnostic-only. No FPS improvement or physical-device optimization is claimed until the results are analyzed and compared on the Huawei Y9s.
