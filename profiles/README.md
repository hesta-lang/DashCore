# Device reference profiles

DashCore keeps device profiles as benchmark metadata and calibration targets. A profile must distinguish between:

- **Recorded hardware:** properties of the real phone.
- **Emulator approximations:** settings that a cloud Android guest can reproduce.
- **Non-emulatable behavior:** real SoC scheduling, GPU drivers, thermal behavior, memory bandwidth and physical frame pacing.

## Huawei Y9s

The initial reference is [huawei-y9s.json](huawei-y9s.json).

Recorded target:
- Huawei Y9s / HiSilicon Kirin 710F
- ARMv8-A, 4× Cortex-A73 (nominal 2.2 GHz) + 4× Cortex-A53 (nominal 1.7 GHz)
- ARM Mali-G51 MP4
- 6 GB physical RAM
- Android 10 / API 29 / arm64-v8a
- 2340×1080 display, 60 Hz

### Important limits

This profile does **not** turn the GitHub cloud emulator into a Kirin 710F or Mali-G51 device. The emulator's GPU remains the host/virtual GPU. Its CPU scheduler and memory subsystem also differ from the phone.

The profile is now loaded by `scripts/autotest_ab.py` and validated by `.github/workflows/autotest-v3-cloud.yml`. AutoTest records the real cloud guest fingerprint and writes `device-profile.json`, `cloud-device-fingerprint.json`, and `device-profile-assessment.json` into the uploaded reports. Each variant's `metrics.json` includes the profile ID and `NOT_EMULATED` marker. The comparison report names the reference hardware and the actual cloud execution environment separately.

It deliberately does not change AutoTest's current screenshot/tap resolution or GPU mode, because doing so without migrating and validating the automation would invalidate its coordinates and comparisons.

### How to use it

1. Keep the same cloud emulator image and test workload for both General Enable states.
2. Require confirmed gameplay before collecting the 600-frame sample.
3. Compare ON and OFF with the same level, route, attempt stimulus and settings.
4. Label cloud results as emulator results, never as Huawei FPS.
5. Validate the same build on the physical Y9s and retain its Geode log for hardware-specific conclusions.

Before applying a density override, capture the actual device density (wm density) from the phone. Do not infer density from the pixel resolution.
