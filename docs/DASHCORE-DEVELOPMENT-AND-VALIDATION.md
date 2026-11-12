# DashCore Development and Validation Protocol

## Purpose

This protocol is the default engineering workflow for DashCore. It prioritizes real performance improvements and reliable bug fixes on the target Android device while keeping Geometry Dash as close to vanilla as possible.

## Target and non-negotiable constraints

- Geometry Dash 2.2081, Geode 5.11.2, Android64 (\`arm64-v8a\`), Android 10 / API 29 minimum.
- Physical reference device: Huawei Y9s, Kirin 710F, Mali-G51 MP4, 6 GB RAM, 2340x1080, 60 Hz.
- Preserve the original Cocos2d renderer, gameplay, physics, draw order, and UI unless a change is explicitly requested.
- Static culling and all performance diagnostics stay OFF by default. Diagnostic builds may enable read-only, bounded, opt-in sampling.
- Never add a hook or detour based on an iOS/Android32 address. Before using a binding, verify that the method exists for the pinned Android64 target and is supported by the Geode bindings.
- Do not detour inline/unsupported methods or guess offsets. If an Android64 address or supported binding is absent, do not hook it.
- Do not change governors, CPU frequencies, schedulers, cpusets, thermal controls, or unrelated launcher/system settings as part of a DashCore optimization.
- Every diagnostic or validation release remains prerelease \`alpha\`. A non-alpha optimization release requires a real optimization and physical validation on the Huawei.

## Standard workflow

### 1. Classify the change

Choose one primary purpose before editing:

- **Diagnosis:** add or adjust read-only evidence collection; no gameplay or rendering behavior change.
- **Bug fix:** correct a reproducible defect with a demonstrated root cause.
- **Optimization:** reduce measured CPU work, allocations, draw overhead, or frame-time spikes without changing intended game behavior.
- **Maintenance:** documentation, CI, packaging, or build-system work with no runtime change.

Do not combine unrelated fixes or optimizations in one change.

### 2. Establish evidence and the baseline

Before proposing a code change:

1. Record the current branch, commit SHA, mod version, Geode version, game version, target ABI, and workflow run URL.
2. Read the complete relevant error/log section, not only the final error line.
3. Identify whether the evidence came from the Huawei or a cloud emulator.
4. For performance work, identify the exact measured phase and scene. Use existing comparable captures where possible; do not add redundant benchmark passes merely to generate more numbers.
5. Keep CPU callback timings, GPU execution, and presented-frame pacing as separate measurements.

A cloud emulator is useful for package installation, startup, gameplay automation, crash detection, and diagnostics-path validation. It is **not** equivalent to the Kirin 710F / Mali-G51 and cannot validate a physical-device FPS or stutter improvement.

### 3. Find the root cause before fixing

Use systematic debugging:

1. Reproduce the issue when practical and record exact steps.
2. Compare the failing revision with the last known working revision.
3. Trace the data/call path to the first incorrect state or unsupported assumption.
4. Verify the affected API against the pinned Geode bindings and current official documentation.
5. State one testable root-cause hypothesis.
6. If the evidence is insufficient, improve diagnosis rather than guessing.

After three unsuccessful minimal fixes, stop and reconsider the architecture with the project owner instead of layering on another speculative patch.

### 4. Make the smallest testable change

- Add or update a regression test before the fix whenever the defect can be tested automatically.
- Change one root cause at a time.
- Prefer existing project patterns and avoid unrelated refactors.
- Preserve original callbacks: forward them exactly once and keep optional instrumentation failure non-fatal.
- Bound all diagnostic traversals, samples, and allocations; report truncation rather than scanning without limits.
- Never make a diagnostic hook mandatory for normal mod startup.

### 5. Run the code-quality and unit-test gates

The Android64 build workflow is the source of truth for the standard static and unit-test checks. It currently runs:

- Cppcheck on the C++ mod source and the C CPU-profile tool.
- Python syntax compilation for the diagnostic and AutoTest scripts.
- Unit tests for CPU-profile parsing, calibration, render-pipeline contract, and gameplay AutoTest validation.

Any failure blocks the change. Fix the first proven root cause and rerun the relevant checks; do not treat a successful static check as proof that the Android binary compiles.

### 6. Build and validate the Android64 package

The required package target is Android64 with minimum SDK 29. Verify:

- The build job completed successfully for the exact commit under review.
- The expected \`.geode\` artifact exists and passes package validation.
- The artifact belongs to the same source SHA being tested.
- Build warnings/errors are reviewed rather than hidden by a green downstream job.

### 7. Run the gameplay-only cloud AutoTest

Use the single gameplay-and-diagnostics AutoTest. Its intended configuration is:

- Static culling OFF.
- Performance Diagnostics OFF.
- Render Pipeline Diagnostics ON only for the bounded diagnostic sample.
- One gameplay session; no A/B or culling-on comparison.
- The test must verify the in-level HUD, required hooks, the complete 180-frame sample, required timing histograms, hierarchy/section summaries, and absence of a detected crash.

A missing final sample marker or missing required diagnostic record means **incomplete**, not passed. A cloud AutoTest pass does not authorize a performance claim for the Huawei.

### 8. Validate on the physical Huawei Y9s

Before calling an optimization successful:

1. Install the exact alpha artifact on the Huawei Y9s.
2. Follow the agreed gameplay scenario and collect the Geode log.
3. Record the level/scene, start position, test conditions, mod version, and commit SHA.
4. Confirm that DashCore loads without new errors and that the intended gameplay scenario works.
5. Compare against an existing baseline only when scene, device, configuration, and measurement method are sufficiently comparable.
6. Evaluate tail latency and hitch counts as well as averages. Do not infer smoothness from one average CPU time or one maximum value.
7. Reject or revise a change that regresses gameplay, stability, input behavior, or frame pacing.

If physical validation has not happened, report the build as **diagnostic/validation only** and retain the \`alpha\` prerelease label.

## Interpreting performance data correctly

- \`CCNode::visit\`, sprite, batch, atlas, update, and \`drawScene\` timings are CPU-duration measurements; they are not GPU execution times.
- Timings from nested callbacks can be inclusive and overlapping. Do not sum them or subtract them as if they were exclusive costs.
- Callback counts, sample counts, averages, and maxima must be interpreted with their sampling stride and profile window.
- A single scene or one device session is evidence for that run, not a universal result.
- FPS and stutter claims require presented-frame evidence from the physical device, not only C++ callback timing or cloud-emulator output.

## Release decision

A change may be described as an **optimization** only when all applicable code-quality, build, AutoTest, and physical-device checks are recorded and the physical evidence supports an improvement without a material regression.

Otherwise, use factual wording such as **diagnostic update**, **validation build**, or **experimental change** and keep the release prerelease \`alpha\`.

## Standard report for each change

Every implementation report should contain:

- **Purpose:** diagnosis, bug fix, optimization, or maintenance.
- **Root cause:** evidence and affected code path.
- **Change:** exact files/behavior changed and what deliberately did not change.
- **API verification:** Geode binding/API evidence for Android64.
- **Checks:** Cppcheck, Python tests, Android64 build, package validation, and AutoTest run URLs with actual conclusions.
- **Physical validation:** device, version/SHA, scenario, log, and measured result — or explicitly \`NOT YET VALIDATED\`.
- **Limitations:** missing metrics, non-comparable captures, or unresolved risks.

## Tooling workflow

- **GitHub:** inspect the current branch and source SHA before edits; use expected-SHA updates and never force-push over concurrent work.
- **Context7:** verify current library/API documentation when API behavior or signatures are uncertain.
- **Superpowers:** use brainstorming/planning for new behavior, systematic debugging for defects or performance regressions, test-driven development for fixes, and verification-before-completion before reporting success.
- **ChatGPT analysis:** inspect logs and profiles, separate evidence from hypotheses, and never label an unverified optimization as successful.
