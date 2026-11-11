#!/usr/bin/env python3
"""Compare an Android cloud-guest CPU probe with measured Huawei Y9s references.

This is a descriptive comparison only. It never applies CPU quotas, clock
limits, affinity changes, or claims that the cloud guest emulates a Kirin 710F.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import statistics
import sys
from datetime import datetime, timezone
from typing import Any

BENCHMARK_ID = "dashcore-kirin-cpu-v1"
TEST_NAMES = (
    "higher_capacity_single_core",
    "lower_capacity_single_core",
    "all_allowed_cores_concurrent",
)


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"Cannot read valid JSON from {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object in {path}")
    return value


def _median(values: Any, label: str) -> float:
    if not isinstance(values, list) or not values:
        raise ValueError(f"Missing numeric samples for {label}")
    try:
        numbers = [float(value) for value in values]
    except (TypeError, ValueError) as exc:
        raise ValueError(f"Invalid numeric samples for {label}") from exc
    if any(value <= 0 for value in numbers):
        raise ValueError(f"Non-positive throughput sample for {label}")
    return float(statistics.median(numbers))


def build_comparison(reference_profile: dict[str, Any],
                     cloud_report: dict[str, Any],
                     sample_id: str | None = None) -> dict[str, Any]:
    if cloud_report.get("benchmark_id") != BENCHMARK_ID:
        raise ValueError("Cloud report benchmark_id does not match the Kirin CPU benchmark.")
    if cloud_report.get("execution_mode") != "adb_android_guest":
        raise ValueError("Observed report must come from an ADB-connected Android cloud guest.")

    calibration = reference_profile.get("cpu_execution_model", {}).get(
        "physical_measurements", {}
    )
    if calibration.get("benchmark_id") != BENCHMARK_ID:
        raise ValueError("Reference profile has no compatible physical CPU measurements.")

    selected_id = sample_id or calibration.get("reference_sample_id")
    samples = calibration.get("samples", [])
    selected = next(
        (item for item in samples if item.get("sample_id") == selected_id),
        None,
    )
    if selected is None:
        raise ValueError(f"Physical reference sample not found: {selected_id}")

    reference_runs = selected.get("run_iterations_per_second", {})
    cloud_benchmarks = cloud_report.get("benchmarks", {})
    cloud_medians = cloud_benchmarks.get("median_per_cpu", {})
    metrics = []
    for test_name in TEST_NAMES:
        reference_values = reference_runs.get(test_name)
        cloud_summary = cloud_medians.get(test_name)
        if not reference_values or not isinstance(cloud_summary, dict):
            continue
        physical_median = _median(reference_values, f"physical {test_name}")
        cloud_median = float(cloud_summary.get("median_aggregate_iterations_per_second", 0))
        if cloud_median <= 0:
            continue
        metrics.append({
            "test": test_name,
            "physical_reference_median_iterations_per_second": round(physical_median, 3),
            "cloud_guest_median_iterations_per_second": round(cloud_median, 3),
            "cloud_vs_physical_percent": round(
                (cloud_median / physical_median - 1.0) * 100.0, 2
            ),
            "physical_samples": len(reference_values),
            "cloud_samples": int(cloud_summary.get("samples", 0)),
        })

    if not metrics:
        raise ValueError("No matching throughput tests were found in both reports.")

    topology = cloud_report.get("topology_assessment", {})
    inventory = cloud_report.get("cpu_inventory", {})
    families: dict[str, int] = {}
    for row in inventory.get("cores", []):
        family = row.get("cpu_family", "unknown")
        families[family] = families.get(family, 0) + 1

    cloud_repetitions = int(cloud_benchmarks.get("repetitions", 0))
    physical_repetitions = int(selected.get("repetitions", 0))
    warnings = []
    if physical_repetitions < 3:
        warnings.append("The selected physical reference has fewer than three repetitions.")
    if cloud_repetitions < 3:
        warnings.append("The cloud guest has fewer than three repetitions; treat its comparison as preliminary.")
    if not cloud_benchmarks.get("affinity_succeeded_for_all_workers", False):
        warnings.append("CPU affinity was not confirmed for every cloud benchmark worker.")
    if topology.get("status") != "ARM_CPU_FAMILY_TOPOLOGY_MATCH":
        warnings.append(
            "The cloud guest does not expose a confirmed 4x Cortex-A73 + 4x Cortex-A53 topology."
        )
    if inventory.get("cpuset_restriction_detected"):
        warnings.append("The cloud profiler process was restricted to a subset of online CPUs.")

    return {
        "schema_version": 1,
        "benchmark_id": BENCHMARK_ID,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "comparison_type": "physical_huawei_y9s_vs_cloud_android_guest",
        "physical_reference": {
            "profile_id": reference_profile.get("profile_id"),
            "sample_id": selected_id,
            "condition": selected.get("condition"),
            "duration_ms": selected.get("duration_ms"),
            "repetitions": physical_repetitions,
        },
        "cloud_guest": {
            "execution_mode": cloud_report.get("execution_mode"),
            "observed_device": cloud_report.get("observed_device", {}),
            "topology_status": topology.get("status"),
            "cpu_family_counts": families,
            "online_cpu_count": len(inventory.get("online_cpu_list", [])),
            "allowed_cpu_count": len(inventory.get("process_allowed_cpu_list", [])),
            "duration_ms": cloud_benchmarks.get("duration_ms"),
            "repetitions": cloud_repetitions,
            "affinity_succeeded_for_all_workers":
                cloud_benchmarks.get("affinity_succeeded_for_all_workers", False),
        },
        "metrics": metrics,
        "warnings": warnings,
        "decision": {
            "status": "COMPARISON_ONLY",
            "hardware_equivalence": "NOT_EMULATED",
            "cpu_budget_derived": False,
            "cpu_quota_or_frequency_limit_applied": False,
            "note": (
                "Throughput ratios describe this integer microbenchmark only. They are not FPS, "
                "not a general CPU equivalence score, and not sufficient to select a CPU quota."
            ),
        },
    }


def render_markdown(report: dict[str, Any]) -> str:
    physical = report["physical_reference"]
    cloud = report["cloud_guest"]
    lines = [
        "# Kirin CPU calibration comparison",
        "",
        f"- Physical reference: {physical.get('profile_id')}",
        f"- Physical sample: {physical.get('sample_id')} ({physical.get('condition')})",
        f"- Cloud execution mode: {cloud.get('execution_mode')}",
        f"- Cloud topology: **{cloud.get('topology_status')}**",
        f"- Cloud benchmark: {cloud.get('duration_ms')} ms x {cloud.get('repetitions')} repetitions",
        f"- Cloud CPU affinity: **{cloud.get('affinity_succeeded_for_all_workers')}**",
        "",
        "| Test | Huawei Y9s median (iter/s) | Cloud guest median (iter/s) | Cloud vs Y9s |",
        "|---|---:|---:|---:|",
    ]
    for metric in report["metrics"]:
        lines.append(
            f"| {metric['test']} | "
            f"{metric['physical_reference_median_iterations_per_second']:.3f} | "
            f"{metric['cloud_guest_median_iterations_per_second']:.3f} | "
            f"{metric['cloud_vs_physical_percent']:+.2f}% |"
        )
    lines += ["", "## Interpretation", ""]
    if report["warnings"]:
        lines.append("Warnings:")
        lines.extend(f"- {warning}" for warning in report["warnings"])
    else:
        lines.append("No structural warning was detected by the comparison checks.")
    lines += [
        "",
        "This comparison is not a Geometry Dash FPS result and does not establish hardware equivalence.",
        "Candidate higher/lower-capacity tests are not assumed to be the same microarchitectures across devices.",
        "No CPU budget, quota, clock override, or governor policy was derived or applied.",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare a cloud Android CPU profile with the measured Huawei Y9s reference."
    )
    parser.add_argument("--reference-profile", required=True, type=pathlib.Path)
    parser.add_argument("--cloud-report", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--sample-id", help="Physical sample ID; defaults to the profile's selected reference.")
    args = parser.parse_args()

    reference = load_json(args.reference_profile)
    cloud = load_json(args.cloud_report)
    comparison = build_comparison(reference, cloud, args.sample_id)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.output_dir / "kirin-cpu-calibration-comparison.json"
    md_path = args.output_dir / "kirin-cpu-calibration-comparison.md"
    json_path.write_text(json.dumps(comparison, indent=2) + "\n", encoding="utf-8")
    md_path.write_text(render_markdown(comparison), encoding="utf-8")
    print(f"Comparison JSON: {json_path}")
    print(f"Comparison summary: {md_path}")
    print("Decision: COMPARISON_ONLY; no CPU budget or limit was applied.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
