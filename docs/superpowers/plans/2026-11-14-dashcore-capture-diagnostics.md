# DashCore Runtime Capture Diagnostics — 0.11.1-alpha

**Goal:** Add low-volume, bounded diagnostics to the live read-only object snapshot collector so physical-device tests can measure original update cost, traversal work, compatibility coverage, CPU capture time, and snapshot storage.

**Scope:** This is a diagnostic alpha requested after the 0.11.0-alpha read-only snapshot build. It intentionally adds a narrow runtime diagnostic summary, superseding the earlier 0.11.0 milestone rule that kept profiling/diagnostic output out of the package. It does not add culling, draw interception, rendering changes, FPS overlays, automatic test behavior, or optimization claims.

**Implementation:**
- Measure the original PlayLayer postUpdate callback and stop that timer before invoking DashCore capture.
- Measure source collection and total collector capture independently with steady_clock.
- Count nodes visited, root objects, candidate sprites, accepted sprites, conservative rejection reasons, traversal limits, and unique texture keys.
- Keep only fixed 512-sample timing windows; recording a frame does not allocate timing storage.
- Aggregate counters and full-level averages; p95 describes the latest 512 frames (or all frames when fewer than 512 exist).
- Report snapshot sprite count and vector element-buffer capacity. Do not call this total process memory; allocator overhead and temporary structures are excluded.
- Emit one summary when PlayLayer exits. Do not write log lines from the per-frame path.
- Keep vanilla Geometry Dash rendering as the only visible draw path and preserve all previous adapter bounds and exclusions.

**Validation:**
- Host tests cover aggregate counters, successful/failed frames, averages, p95, snapshot high-water marks, texture high-water marks, and the rolling timing window.
- Product contract tests verify bounded storage, no per-frame logging, lifecycle-only summary output, original-update timing, and absence of draw hooks.
- Android64 Release build validates the Geode adapter and package for Geometry Dash 2.2081 / Geode 5.11.2 / Android API 29.
- Physical Huawei Y9s tests are required before drawing conclusions about capture cost. Cloud CI is not physical performance evidence.

**Interpretation limits:**
- Node/object/sprite totals are accumulated per captured frame, so objects that persist are counted repeatedly. They represent work performed, not unique level contents.
- CPU wall-clock durations are not GPU time or full-frame time. The update metric measures the original callback; capture metrics measure DashCore code after that callback.
- Snapshot vector capacity bytes cover only the snapshot vector buffer, not total heap/process memory.
- Because capture itself runs synchronously in postUpdate, this diagnostic build may add CPU work. No optimization or FPS improvement is claimed.
