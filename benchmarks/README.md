# DashCore Benchmark

Controlled local Geometry Dash level for repeatable DashCore diagnostics.

- Target game: Geometry Dash 2.2081
- Target loader: Geode 5.11.2
- Target architecture: Android64 / arm64-v8a
- Level format: GDShare-compatible XML plist (.gmd)
- Runtime status: NOT YET TESTED IN GEOMETRY DASH

## Sections

| Section | Contents | Purpose |
|---|---:|---|
| Baseline | 24 blocks | Low-load reference |
| Static density | 288 blocks | Large static GameObject count |
| Layered overdraw | 1,280 blocks | Two offset layers and multiple Z layers |
| Grouped objects | 90 blocks | Three target groups for trigger scenarios |
| Color channel | 24 blocks | Channel 2 target objects |
| Trigger sequence | 5 triggers | Color, Move, Pulse, Alpha and Toggle |
| Tail | 240 blocks | Sustained scene population after triggers |

The level contains more than 1,900 objects. It uses the built-in ground and keeps the player path clear; it measures scene/rendering load, not difficulty.

## Generate

Run: python3 benchmarks/generate_benchmark.py --output-dir dist

The generator writes:
- DashCore-Benchmark.gmd — importable level file in XML plist format.
- DashCore-Benchmark.levelstring.txt — uncompressed inner level string.
- DashCore-Benchmark.levelstring.b64 — compressed level string used by the game.
- benchmark-manifest.json — object counts, hashes and validation status.

## Import and run

Import DashCore-Benchmark.gmd with a Geometry Dash 2.2081-compatible level importer that accepts GDShare .gmd files. Keep DashCore experimental culling disabled.

1. Open the imported level and play from the beginning.
2. Allow the complete route to run; do not use StartPos for the first baseline pass.
3. Capture the complete Geode log after the level has loaded and the sampling window has completed.
4. Repeat under the same graphics settings and comparable device/thermal conditions.
5. Compare logs from the same device and same game/Geode versions.

The trigger sequence targets isolated groups/channels. If a trigger behaves unexpectedly or an object disappears incorrectly, stop the run and keep the log. This is a diagnostic fixture, not a gameplay level.

## What validation means

The generator validates the plist, gzip/base64 round-trip, level header, object property pairs, finite coordinates, object totals and expected trigger IDs. The GitHub workflow uploads the generated files as an artifact.

A structural PASS does not mean the level has been opened in Geometry Dash. Runtime validation remains NOT_RUN until the level is imported and played on a compatible game installation. GitHub-hosted Android ARM64 emulator execution is not assumed.

## Safety and limitations

- This benchmark does not change DashCore source code or enable rendering culling.
- It does not alter player physics, controls, menus or game settings.
- It does not yet include a particle-emitter section; particle object IDs and their 2.2081 properties must be verified before adding them.
- Timing results are meaningful only when compared on the same device, with the same graphics settings and comparable thermal/background conditions.
