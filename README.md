# DashCore — Android rendering optimization

## 0.10.7-alpha — update timing and object-section diagnostics

- Adds bounded CPU timing around the original `PlayLayer::postUpdate` callback during the opt-in 180-frame render-pipeline sample.
- Reports update callback count, average and maximum CPU duration, slow callbacks, and the observed `dt` range separately from draw-scene timing.
- Adds a one-shot read-only snapshot of `m_activeObjects` and the nested `m_sections` containers: active vector slots/counters, outer section slots, scanned cells, empty/null cells, raw object-reference slots, and maximum per-cell occupancy.
- The section scan reads container sizes only. It does not dereference stored `GameObject*` values, change object membership, or alter rendering/gameplay/UI. Caps: 512 outer slots and 16,384 cells.
- Direct hooks for `addToObjectsToShow`, `reorderObjectSection`, and `updateAllObjectSection` are not used because the 2.2081 binding map does not provide Android hook addresses for these inline/unsupported methods.
- All new diagnostics remain OFF by default. This is still a diagnostic-only alpha; it includes no runtime optimization and claims no FPS improvement.
- **Planned next milestone:** `0.11.0-alpha` is the first experimental optimization release, selected only after the new measurements identify a safe target and validated on the physical Huawei Y9s.

## 0.10.6-alpha — GameObject visual-hierarchy diagnostics

- Extends the opt-in render-pipeline profile with a one-shot read-only inventory of every usable entry in `PlayLayer::m_objects` after the 180-frame render sample.
- Traverses each GameObject's Cocos child subtree and records direct children, descendant nodes, descendant `CCSprite` instances, nested GameObjects, batch-node ancestors, and subtree depth.
- Reports five subtree-size buckets and up to eight objects with the largest child hierarchies to identify where sprite/node fan-out is concentrated.
- Applies per-object (4,096 nodes), per-object depth (64), and total (250,000 nodes) safety caps; reports truncation and scan duration.
- Per-root descendant counts can overlap when GameObjects are nested. A `CCSpriteBatchNode` ancestor is a hierarchy clue, not proof of actual atlas membership.
- The scan is read-only and runs once after the render sample. It does not alter node visibility, transforms, draw order, batching, gameplay, or UI.
- Performance Diagnostics and experimental static culling remain separately controlled; culling remains OFF by default.
- This remains diagnostic-only. No FPS improvement or physical-device optimization is claimed until the results are analyzed and compared on the Huawei Y9s.
