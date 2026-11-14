## 0.11.1-alpha — bounded capture diagnostics
- Adds per-level counts for nodes, root objects, candidate/accepted sprites, unsupported sprite categories, traversal limits, and unique texture keys.
- Measures total capture and source-collection CPU wall-clock time separately, reporting full-level averages, maximums, and p95 for a fixed 512-frame rolling window.
- Reports the last/peak snapshot sprite count and vector-capacity estimate; this is not total process memory.
- Emits one summary on level exit instead of logging every frame.
- Keeps the parallel pipeline read-only. Vanilla rendering remains the only visible draw path; no FPS or optimization claim is made.

## 0.11.0-alpha — live read-only object snapshots
- Adds a conservative Geode source adapter over GJBaseGameLayer::m_objects and the main object layer.
- Traverses copied child arrays in z-order/order-of-arrival order without calling Cocos sorting functions or mutating the live node tree.
- Captures value-only descriptors for unbatched sprites with supported blending, uniform quad colors, zero vertex depth, axis-aligned geometry, rectangular normalized UVs, no atlas rotation/flip, and no vertex modifications.
- Refreshes snapshots after the original PlayLayer::postUpdate(float) callback and clears them on level exit. Engine pointers never enter persistent coordinator or snapshot state.
- Keeps all visible drawing vanilla. CPU geometry preparation and the GLES upload backend remain disconnected from gameplay; no performance gain is claimed.
