## 0.11.0-alpha — live read-only object snapshots
- Adds a conservative Geode source adapter over GJBaseGameLayer::m_objects and the main object layer.
- Traverses copied child arrays in z-order/order-of-arrival order without calling Cocos sorting functions or mutating the live node tree.
- Captures value-only descriptors for unbatched sprites with supported blending, uniform quad colors, zero vertex depth, axis-aligned geometry, rectangular normalized UVs, no atlas rotation/flip, and no vertex modifications.
- Refreshes snapshots after the original PlayLayer::postUpdate(float) callback and clears them on level exit. Engine pointers never enter persistent coordinator or snapshot state.
- Keeps all visible drawing vanilla. CPU geometry preparation and the GLES upload backend remain disconnected from gameplay; no performance gain is claimed.

