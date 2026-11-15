## 0.12.0-alpha — cgroup/RLIMIT diagnostics and isolated GLES compute probe
- Adds read-only cgroup v2 and v1 memory limit/current sampling using the current process membership where exposed.
- Records process VmSize and soft/hard RLIMIT_AS and RLIMIT_RSS values.
- Extends DashCore and launcher JSONL diagnostics with validity/unlimited flags so unavailable and unlimited values are distinguishable.
- Adds an opt-in one-shot GLES 3.1 compute/SSBO smoke test using four 32-bit outputs (16-byte buffer).
- Preserves and restores the current GL program and shader-storage-buffer bindings; does not issue draw calls or inspect level objects.
- Keeps the original Cocos renderer as the only visible renderer and leaves object capture disabled by default.
- Diagnostic prerelease only; no FPS or optimization claim.

## 0.11.9-alpha — adaptive memory budget diagnostics
- Adds read-only Android/Linux procfs sampling for system available RAM and process RSS/PSS/peak RSS.
- Calculates a conservative renderer planning budget with a dynamic OS safety reserve, a 64 MiB ceiling, and a 16 MiB per-buffer ceiling.
- Samples at level start and once per 600 frames; no per-frame procfs I/O is added.
- Fails closed when memory information is missing or system memory pressure is high.
- Does not allocate/reserve memory or change the original Cocos renderer or GPU draw path.
- Keeps object capture disabled by default. This is a diagnostics/validation prerelease, not a performance release.

## 0.11.8-alpha — restore Geode's original renderer
- Removes all DashCore Bismuth sprite/object and trigger-state adapters.
- Removes the custom GLES backend, GL buffer creation/upload path, GLES capability probing, and GLESv3/EGL link dependencies.
- Removes the unused Android Bismuth GL capability bridge and its documentation/tests.
- Keeps the original Geometry Dash/Geode Cocos renderer as the only visible draw path.
- Keeps optional CPU-only diagnostics, disabled by default. No performance or FPS claim is made.

## 0.11.5-alpha — indexed capture and A/B diagnostic modes
- Adds bounded read-only level object/sprite capture and per-level CPU timing summaries.
- Keeps capture disabled by default and leaves vanilla Cocos rendering as the only visible draw path.
- Measures the original PlayLayer::postUpdate callback separately from optional snapshot capture.

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
