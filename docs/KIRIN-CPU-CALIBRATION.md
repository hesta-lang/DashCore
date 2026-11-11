# Kirin 710F CPU calibration for AutoTest

## Objective

The Huawei Y9s target is an eight-core Kirin 710F: four Cortex-A73
performance cores and four Cortex-A53 efficiency cores, with nominal maximum
frequencies of approximately 2.2 GHz and 1.7 GHz respectively. The physical
device reports Android 10/API 29, ARM64, and approximately 5.7 GB of usable
system memory.

A generic Android emulator is not a Kirin 710F recreation merely because it
has eight virtual CPUs, 6 GB of guest RAM, or a reported 2.2 GHz clock. Those
settings do not reproduce the cluster-aware scheduler, per-cluster DVFS,
caches, memory bandwidth, Kirin interconnect, vendor kernel, GPU, or physical
device behavior.

## Implemented calibration path

The benchmark `dashcore-kirin-cpu-v1` has two parts:

- `tools/kirin_cpu_bench.c` is compiled for Android ARM64/API 29. It performs a
  deterministic integer workload on worker threads pinned to selected CPUs.
- `scripts/kirin_cpu_profile.py` discovers online and process-allowed CPU
  lists, ARM CPU part IDs, maximum/current frequencies, scheduler capacity,
  cluster IDs, Android identity and available RAM. It measures high-capacity
  candidates, low-capacity candidates and all CPUs available to the process.
- `scripts/compare_kirin_cpu_profiles.py` compares a cloud guest report with
  the selected physical reference sample and writes JSON and Markdown reports.
  The comparison is descriptive: it does not assign a CPU quota or claim
  that the cloud guest is equivalent to a Kirin 710F.

ARM CPU part IDs are the strongest available family clue: `0xd09` identifies
Cortex-A73 and `0xd03` identifies Cortex-A53. If the kernel does not expose
those IDs, frequency/capacity groups are candidates, not proof of identity.

## Physical Huawei Y9s measurements (2026-11-11)

Four local Termux reports are stored in
`profiles/huawei-y9s.json` under
`cpu_execution_model.physical_measurements.samples`. The selected numerical
reference is `rested-idle-2026-11-11` (4000 ms per test, three repetitions).
The current-frequency columns below are one-point snapshots, not sustained
frequency measurements.

| Measurement | A73 candidate (iter/s) | A53 candidate (iter/s) | All cores (iter/s) |
|---|---:|---:|---:|
| Initial exploratory sample | 172.27 M | 141.55 M | 1,222.77 M |
| Variable-repeatability sample | 187.17 M | 136.95 M | 1,039.56 M |
| Selected idle reference | **197.57 M** | **137.37 M** | **1,339.44 M** |
| After Geometry Dash session | 185.68 M | 140.41 M | 1,098.99 M |

The after-game sample differs from the selected idle reference by -6.02% for
the higher-capacity single-core test, +2.22% for the lower-capacity test, and
-17.95% for the all-core test. This is a descriptive difference between two
sample sequences; it does not prove thermal throttling or identify a single
cause. Exact play duration and device temperature were not recorded.

The CPU probe does not collect temperature histories. Thermal state is not a
gate for this calibration stage, and this document does not infer throttling
from ambient room conditions or a single current-frequency reading. The
selected idle reference is a numerical benchmark reference, not a claim
about a measured chip temperature.

The physical report confirms the Huawei Y9s identity, all eight online and
allowed CPUs, successful affinity, four Cortex-A53 cores (CPU 0-3) and four
Cortex-A73 cores (CPU 4-7). The observed scheduler capacities were 486 and
1024 respectively. Scheduler capacity is not a direct throughput multiplier.

## Cloud and physical-device modes

The Kirin CPU Profile Tool workflow builds an ARM64 executable and uploads the
Termux-ready artifact `DashCore-Kirin-CPU-Profile-Tool`. For the physical
Huawei Y9s, run the tool locally in Termux and retain the resulting JSON and
Markdown reports. The local Y9s report is the physical reference; the AutoTest
report describes only the cloud Android guest.

The AutoTest workflow runs the same ARM64 integer workload against its Android
guest for 4000 ms per test and three repetitions. It then runs
`scripts/compare_kirin_cpu_profiles.py` against the selected physical
reference. The comparison artifact is saved under
`reports/autotest-fps/kirin-cpu-calibration-comparison/`.

The comparison checks the same benchmark test names and reports cloud-to-phone
throughput ratios. Higher/lower-capacity candidate tests are not assumed to
represent identical microarchitectures on different devices. If the cloud
guest does not expose the same ARM CPU part IDs and topology, the report marks
that limitation rather than relabeling its cores as Cortex-A73/A53.

## AutoTest integration

The AutoTest FPS runner now consumes the selected physical CPU sample through
`scripts/kirin_autotest_calibration.py`. It writes a compact reference into
`device-profile-assessment.json`, each per-pass `metrics.json`, the
`fps-comparison.json` reports and `fps-repeated-summary.json`. The report
includes the selected sample ID, duration/repetition count, physical A73/A53
candidate medians, and the concurrent all-core median.

This integration is intentionally descriptive. It does not change the cloud
guest's CPU count, affinity, clock, governor, quota, scheduler, GPU, display,
or memory. It does not normalize FPS using CPU microbenchmark ratios. The
cloud CPU probe and its physical-vs-cloud comparison remain separate artifacts
under `kirin-cpu-profile-cloud/` and
`kirin-cpu-calibration-comparison/`, because those measurements are not FPS.

A cloud-only AutoTest run now reports physical Huawei Y9s FPS validation as
`NOT_RUN_CLOUD_ONLY`, not as completed validation. The physical phone still
needs a same-level, same-route baseline/with-mod run before any optimization
can be treated as validated.

## Calibration decision

- The physical reference is now recorded; no additional temperature test is
  required for this stage.
- Compare the cloud guest's topology, allowed CPUs, affinity, repetition count
  and throughput against the physical reference.
- Treat the result as a microbenchmark comparison only, not as Geometry Dash
  FPS or proof of equivalent game performance.
- Do not apply CPU quotas, virtual clock overrides, governor changes or
  scheduler-policy changes from this comparison alone. A proposed emulator
  budget must be evaluated separately against repeatable AutoTest behavior.

A CPU profile cannot recreate the Mali-G51 MP4. GPU/rendering calibration is a
separate task. Keep diagnostic/validation releases at the `alpha` stage; this
tooling is not evidence of an optimization.
