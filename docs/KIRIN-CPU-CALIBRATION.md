# Kirin 710F CPU calibration for AutoTest

## Objective

The Huawei Y9s target is an eight-core Kirin 710F: four Cortex-A73
performance cores with a nominal maximum of 2.2 GHz and four Cortex-A53
efficiency cores with a nominal maximum of 1.7 GHz. It has 6 GB RAM and
Android 10/API 29.

A generic Android emulator is not a Kirin 710F recreation merely because it
has eight virtual CPUs, 6 GB of guest RAM, or a reported 2.2 GHz clock. Those
settings do not reproduce the cluster-aware scheduler, per-cluster DVFS,
caches, memory bandwidth, Kirin interconnect, vendor kernel, GPU, or thermal
behavior.

## Implemented calibration path

The benchmark dashcore-kirin-cpu-v1 has two parts:

- tools/kirin_cpu_bench.c is compiled for Android ARM64/API 29. It performs a
  deterministic integer workload on worker threads pinned to selected CPUs.
- scripts/kirin_cpu_profile.py discovers online and process-allowed CPU lists,
  ARM CPU part IDs, max/current frequencies, scheduler capacity, cluster IDs,
  Android identity and available RAM. It measures higher-capacity candidates,
  lower-capacity candidates, and all CPUs available to the process.

ARM CPU part IDs are the strongest available family clue: 0xd09 identifies
Cortex-A73 and 0xd03 identifies Cortex-A53. If the kernel does not expose
those IDs, frequency/capacity groups are candidates, not proof of identity.

## Cloud and physical-device modes

The Kirin CPU Profile Tool workflow builds an ARM64 executable and uploads the
Termux-ready artifact DashCore-Kirin-CPU-Profile-Tool. The AutoTest cloud
workflow also runs the profiler against its Android guest and includes that
separate cloud measurement in its report artifact.

For the physical Huawei Y9s, download the tool artifact, extract it on the
phone, install Python in Termux, and run
bash run-kirin-cpu-profile-termux.sh. Exact steps are included in the tool
package README.

The cloud report describes the cloud guest. Only a local Y9s report is accepted
as the reference CPU measurement.

## Calibration gate

No CPU quota, clock override, governor change, or assumed big/LITTLE ratio is
applied. First collect the physical Y9s report and compare:

1. Confirm observed device identity and eight online CPUs.
2. Confirm four Cortex-A73 and four Cortex-A53 part IDs, or document the
   weaker frequency/capacity-only inference if part IDs are unavailable.
3. Compare per-core and all-core throughput over repeated runs.
4. Check whether thermal changes cause sustained throughput to decline.
5. Only then derive and validate an emulator-side CPU budget, without calling
   it a full Kirin emulation.

A CPU profile cannot recreate the Mali-G51 MP4. GPU/rendering calibration is a
separate task. Keep DashCore at alpha; this tooling is not evidence of an
optimization.
