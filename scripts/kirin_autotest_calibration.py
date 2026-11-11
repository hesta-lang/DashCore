#!/usr/bin/env python3
"""Build a safe, descriptive bridge from the physical Kirin profile into AutoTest."""
from __future__ import annotations

import math
import statistics
from typing import Any

BENCHMARK_ID = "dashcore-kirin-cpu-v1"
REFERENCE_TESTS = (
    "higher_capacity_single_core",
    "lower_capacity_single_core",
    "all_allowed_cores_concurrent",
)


def build_physical_cpu_reference(profile: dict[str, Any]) -> dict[str, Any]:
    """Summarize the selected physical sample without tuning the cloud guest."""
    if not isinstance(profile, dict):
        raise ValueError("Device profile must be a JSON object.")

    execution_model = profile.get("cpu_execution_model", {})
    calibration = execution_model.get("physical_measurements", {})
    if calibration.get("benchmark_id") != BENCHMARK_ID:
        raise ValueError("Profile has no compatible physical Kirin CPU reference.")

    sample_id = calibration.get("reference_sample_id")
    samples = calibration.get("samples", [])
    sample = next(
        (item for item in samples if item.get("sample_id") == sample_id),
        None,
    )
    if sample is None:
        raise ValueError(f"Selected physical CPU reference sample not found: {sample_id}")

    minimum_repetitions = int(
        execution_model.get("calibration", {}).get("minimum_repetitions", 3)
    )
    repetitions = int(sample.get("repetitions", 0))
    if minimum_repetitions < 1 or repetitions < minimum_repetitions:
        raise ValueError(
            f"Physical reference has {repetitions} repetitions; "
            f"at least {minimum_repetitions} are required."
        )

    raw_metrics = sample.get("run_iterations_per_second", {})
    medians: dict[str, float] = {}
    sample_counts: dict[str, int] = {}
    for test_name in REFERENCE_TESTS:
        values = raw_metrics.get(test_name)
        if not isinstance(values, list) or len(values) < minimum_repetitions:
            raise ValueError(
                f"Physical reference needs at least {minimum_repetitions} samples for {test_name}."
            )
        try:
            numeric_values = [float(value) for value in values]
        except (TypeError, ValueError) as exc:
            raise ValueError(f"Invalid physical throughput values for {test_name}.") from exc
        if any(not math.isfinite(value) or value <= 0 for value in numeric_values):
            raise ValueError(f"Non-positive or non-finite physical throughput for {test_name}.")
        medians[test_name] = round(statistics.median(numeric_values), 3)
        sample_counts[test_name] = len(numeric_values)

    device = profile.get("device", {})
    identity = calibration.get("device_identity", {})
    return {
        "status": "PHYSICAL_REFERENCE_CONTEXT_ONLY",
        "benchmark_id": BENCHMARK_ID,
        "reference_device": {
            "profile_id": profile.get("profile_id"),
            "display_name": profile.get("display_name"),
            "manufacturer": identity.get("manufacturer", device.get("manufacturer")),
            "model": identity.get("model", device.get("model")),
            "soc_reported": identity.get("soc_reported"),
            "soc_profile": device.get("soc", {}).get("name"),
        },
        "selected_sample": {
            "sample_id": sample_id,
            "condition": sample.get("condition"),
            "duration_ms": sample.get("duration_ms"),
            "repetitions": repetitions,
        },
        "core_inventory": calibration.get("core_inventory", {}),
        "median_iterations_per_second": medians,
        "sample_counts": sample_counts,
        "interpretation": {
            "hardware_equivalence": "NOT_EMULATED",
            "cpu_budget_derived": False,
            "cloud_cpu_limits_applied": False,
            "fps_normalization_applied": False,
            "used_as_performance_scaling_factor": False,
        },
        "limitations": [
            "These are physical Huawei Y9s integer-microbenchmark reference values, not cloud-guest measurements.",
            "The cloud guest does not become a Kirin 710F through this profile.",
            "The values are context for interpreting AutoTest reports; they do not set CPU quotas, clocks, affinity, or FPS correction factors.",
            "The microbenchmark is not a Geometry Dash FPS measurement.",
        ],
    }
