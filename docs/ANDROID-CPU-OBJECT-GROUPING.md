# DashCore 0.16.0-alpha — CPU synthetic object grouping

## Goal

Validate a CPU-side data layout that can later be serialized into GPU storage buffers. This stage is not connected to the Geometry Dash object collection or Cocos rendering.

## Data model

Each synthetic input record contains:
- non-zero stable object ID
- X/Y position
- rotation in degrees
- X/Y scale
- object type ID
- state flags

The output uses structure-of-arrays vectors for each field. Objects are ordered by ascending type ID. Their input order is preserved within a type. A group-range array describes contiguous ranges by type.

## Bounds and validation

- Maximum accepted records: 4,096.
- Device diagnostic workload: exactly 1,024 synthetic records and 8 types.
- Rejects zero IDs, duplicate IDs, and non-finite transforms.
- Empty input is valid and produces empty arrays.
- Validates all array lengths, group contiguity, ascending type ordering, finite transform values, and unique non-zero IDs.
- The CPU test covers stable ordering, data alignment, empty input, invalid identifiers, NaN input, invalid output, and input above the configured bound.

## Runtime setting

The Geode setting `cpu-object-grouping-diagnostic` is disabled by default. When enabled, it runs once from the PlayLayer update hook and logs one `DashCore_CPU_OBJECT_GROUPING` record.

Only synthetic data is generated. The diagnostic does not inspect `PlayLayer::m_objects`, Cocos nodes, sprites, triggers, player state, or level contents. It makes no GL/EGL calls, allocates no SSBO, dispatches no Compute shader, and issues no draw call.

## Acceptance

1. Run the source test workflow and Android64 build workflow.
2. Install the resulting `.geode` only after the build succeeds.
3. First test with every diagnostic disabled.
4. Enable only the CPU object grouping diagnostic and confirm `status=passed`, `objects=1024`, `grouped_objects=1024`, `groups=8`, and `validated=true`.
5. Disable it again before testing other probes.

No FPS improvement or render optimization is claimed in this stage.
