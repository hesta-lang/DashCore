#!/usr/bin/env python3
"""Collect an Android CPU topology and throughput profile through ADB or Termux.

This is a calibration probe, not a Kirin emulator. It never changes CPU clocks,
governors, system affinity, game settings, or persistent device configuration.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import shlex
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from typing import Any

BENCHMARK_ID = "dashcore-kirin-cpu-v1"
CPU_PARTS = {"0xd03": "Cortex-A53", "0xd09": "Cortex-A73"}


def parse_cpu_list(value: str) -> list[int]:
    cpus: set[int] = set()
    for item in value.strip().split(","):
        item = item.strip()
        if not item:
            continue
        if "-" in item:
            first, last = item.split("-", 1)
            start, end = int(first), int(last)
            if start < 0 or end < start:
                raise ValueError(f"Invalid CPU range: {item}")
            cpus.update(range(start, end + 1))
        else:
            cpu = int(item)
            if cpu < 0:
                raise ValueError(f"Invalid CPU ID: {item}")
            cpus.add(cpu)
    return sorted(cpus)


def parse_cpuinfo(text: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for block in re.split(r"\n\s*\n", text.strip()):
        fields: dict[str, str] = {}
        for line in block.splitlines():
            if ":" in line:
                key, value = line.split(":", 1)
                fields[key.strip().lower()] = value.strip()
        if "processor" not in fields:
            continue
        part = fields.get("cpu part", "").lower()
        records.append({
            "processor": fields.get("processor"),
            "implementer": fields.get("cpu implementer"),
            "part": part or None,
            "family": CPU_PARTS.get(part, "unknown"),
            "architecture": fields.get("cpu architecture"),
            "model_name": fields.get("model name") or fields.get("hardware"),
        })
    return records


def group_cpus(core_rows: list[dict[str, Any]]) -> dict[str, Any]:
    by_family: dict[str, list[int]] = {}
    for row in core_rows:
        family = row.get("cpu_family")
        if family in ("Cortex-A53", "Cortex-A73"):
            by_family.setdefault(family, []).append(row["cpu"])
    if len(by_family) >= 2:
        return {
            "method": "arm_cpu_part",
            "groups": [
                {"name": name, "cpus": sorted(ids)}
                for name, ids in sorted(by_family.items())
            ],
        }

    frequencies = sorted({
        row["cpuinfo_max_freq_khz"]
        for row in core_rows
        if isinstance(row.get("cpuinfo_max_freq_khz"), int)
        and row["cpuinfo_max_freq_khz"] > 0
    })
    if len(frequencies) >= 2:
        return {
            "method": "cpuinfo_max_freq",
            "groups": [
                {
                    "name": "higher-frequency-candidate" if freq == frequencies[-1]
                            else "lower-frequency-candidate" if freq == frequencies[0]
                            else f"frequency-{freq}-khz",
                    "cpus": sorted(row["cpu"] for row in core_rows
                                   if row.get("cpuinfo_max_freq_khz") == freq),
                    "max_frequency_khz": freq,
                }
                for freq in frequencies
            ],
        }

    capacities = sorted({
        row["cpu_capacity"]
        for row in core_rows
        if isinstance(row.get("cpu_capacity"), int) and row["cpu_capacity"] > 0
    })
    if len(capacities) >= 2:
        return {
            "method": "cpu_capacity",
            "groups": [
                {
                    "name": "higher-capacity-candidate" if capacity == capacities[-1]
                            else "lower-capacity-candidate" if capacity == capacities[0]
                            else f"capacity-{capacity}",
                    "cpus": sorted(row["cpu"] for row in core_rows
                                   if row.get("cpu_capacity") == capacity),
                    "capacity": capacity,
                }
                for capacity in capacities
            ],
        }
    return {"method": "unavailable-or-homogeneous", "groups": []}


class Target:
    def __init__(self, serial: str | None):
        self.serial = serial
        self.remote_binary = "/data/local/tmp/dashcore-kirin-cpu-bench"

    def shell(self, command: str, check: bool = False, timeout: int = 30) -> str:
        args = (["adb", "-s", self.serial, "shell", command] if self.serial
                else ["/bin/sh", "-c", command])
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        if check and result.returncode != 0:
            raise RuntimeError(
                f"Target command failed ({result.returncode}): {command}\n"
                f"{result.stdout[-2000:]}"
            )
        return result.stdout.strip()

    def read_file(self, path: str) -> str | None:
        if not self.serial:
            try:
                return pathlib.Path(path).read_text(
                    encoding="utf-8", errors="replace"
                ).strip()
            except OSError:
                return None
        output = self.shell(f"cat {shlex.quote(path)}", check=False)
        if not output or output.startswith("cat:"):
            return None
        return output

    def getprop(self, key: str) -> str | None:
        if self.serial:
            return self.shell(f"getprop {shlex.quote(key)}", check=False) or None
        result = subprocess.run(
            ["/system/bin/getprop", key], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=10,
        )
        return result.stdout.strip() or None

    def install_binary(self, binary: pathlib.Path) -> str:
        if self.serial:
            subprocess.run(
                ["adb", "-s", self.serial, "push", str(binary), self.remote_binary],
                check=True, timeout=120,
            )
            self.shell(f"chmod 755 {self.remote_binary}", check=True)
            return self.remote_binary
        return str(binary.resolve())

    def run_benchmark(self, binary_path: str, cpus: list[int],
                      duration_ms: int) -> list[dict[str, Any]]:
        cpu_arg = ",".join(str(cpu) for cpu in cpus)
        if self.serial:
            command = (
                f"{shlex.quote(binary_path)} --cpus {shlex.quote(cpu_arg)} "
                f"--duration-ms {duration_ms}"
            )
            output = self.shell(command, check=True, timeout=duration_ms / 1000 + 45)
        else:
            result = subprocess.run(
                [binary_path, "--cpus", cpu_arg, "--duration-ms", str(duration_ms)],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=duration_ms / 1000 + 45,
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"Local CPU probe failed ({result.returncode}): {result.stdout[-2000:]}"
                )
            output = result.stdout
        rows = []
        for line in output.splitlines():
            try:
                item = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(item, dict) and "cpu" in item:
                rows.append(item)
        if len(rows) != len(cpus):
            raise RuntimeError(
                f"CPU probe returned {len(rows)} worker records for {len(cpus)} CPUs. "
                f"Output: {output[-2000:]}"
            )
        return rows


def collect_core_rows(target: Target, online_cpus: list[int],
                      cpuinfo_records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_id = {
        int(item["processor"]): item
        for item in cpuinfo_records
        if str(item.get("processor", "")).isdigit()
    }
    rows = []
    for cpu in online_cpus:
        base = f"/sys/devices/system/cpu/cpu{cpu}"

        def read_int(relative: str) -> int | None:
            value = target.read_file(f"{base}/{relative}")
            try:
                return int(value.strip()) if value else None
            except (TypeError, ValueError):
                return None

        info = by_id.get(cpu, {})
        rows.append({
            "cpu": cpu,
            "cpu_family": info.get("family", "unknown"),
            "cpu_part": info.get("part"),
            "cpu_implementer": info.get("implementer"),
            "cpuinfo_max_freq_khz": read_int("cpufreq/cpuinfo_max_freq"),
            "scaling_cur_freq_khz": read_int("cpufreq/scaling_cur_freq"),
            "cpu_capacity": read_int("cpu_capacity"),
            "cluster_id": read_int("topology/cluster_id"),
        })
    return rows


def read_reference_profile(path: pathlib.Path) -> dict[str, Any]:
    profile = json.loads(path.read_text(encoding="utf-8"))
    soc = profile["device"]["soc"]
    topology = soc["cpu_topology"]
    return {
        "profile_id": profile["profile_id"],
        "manufacturer": profile["device"].get("manufacturer"),
        "model": profile["device"].get("model"),
        "soc_name": soc.get("name"),
        "architecture": soc.get("cpu_architecture"),
        "big_core_count": int(topology["big_cores"]),
        "big_core_family": topology["big_core_family"],
        "big_core_nominal_mhz": int(topology["big_core_nominal_mhz"]),
        "little_core_count": int(topology["little_cores"]),
        "little_core_family": topology["little_core_family"],
        "little_core_nominal_mhz": int(topology["little_core_nominal_mhz"]),
        "ram_mb": int(profile["device"]["memory"]["physical_ram_mb"]),
        "android_api": int(profile["android"]["api_level"]),
        "abi": profile["android"]["abi"],
    }


def build_topology_assessment(reference: dict[str, Any], actual: dict[str, Any],
                              core_rows: list[dict[str, Any]],
                              grouping: dict[str, Any]) -> dict[str, Any]:
    families: dict[str, int] = {}
    for row in core_rows:
        family = row.get("cpu_family", "unknown")
        families[family] = families.get(family, 0) + 1

    expected_count = reference["big_core_count"] + reference["little_core_count"]
    online_match = len(core_rows) == expected_count
    family_known = families.get(reference["big_core_family"], 0) > 0 and \
                   families.get(reference["little_core_family"], 0) > 0
    family_counts_match = (
        families.get(reference["big_core_family"], 0) == reference["big_core_count"]
        and families.get(reference["little_core_family"], 0) == reference["little_core_count"]
    )
    groups = grouping.get("groups", [])
    high_group = low_group = None
    if grouping.get("method") == "cpuinfo_max_freq" and len(groups) >= 2:
        ordered = sorted(groups, key=lambda item: item.get("max_frequency_khz", 0))
        low_group, high_group = ordered[0], ordered[-1]

    target_freq_match = False
    frequency_counts_match = False
    if high_group and low_group:
        high_mhz = high_group["max_frequency_khz"] / 1000.0
        low_mhz = low_group["max_frequency_khz"] / 1000.0
        target_freq_match = (
            abs(high_mhz - reference["big_core_nominal_mhz"])
            <= reference["big_core_nominal_mhz"] * 0.10
            and abs(low_mhz - reference["little_core_nominal_mhz"])
            <= reference["little_core_nominal_mhz"] * 0.10
        )
        frequency_counts_match = (
            len(high_group["cpus"]) == reference["big_core_count"]
            and len(low_group["cpus"]) == reference["little_core_count"]
        )

    if not online_match:
        status = "CPU_COUNT_MISMATCH"
    elif family_known and family_counts_match:
        status = "ARM_CPU_FAMILY_TOPOLOGY_MATCH"
    elif target_freq_match and frequency_counts_match:
        status = "FREQUENCY_TOPOLOGY_APPROXIMATION_MATCH"
    else:
        status = "TOPOLOGY_NOT_VERIFIED"

    manufacturer = str(actual.get("manufacturer") or "").lower()
    model = str(actual.get("model") or "").lower()
    soc_name = str(actual.get("soc_model") or "").lower()
    identity = (
        "huawei" in manufacturer
        and ("stk-lx3" in model or "y9s" in model)
        and ("kirin 710" in soc_name or family_counts_match)
    )
    return {
        "status": status,
        "online_cpu_count": len(core_rows),
        "expected_cpu_count": expected_count,
        "online_cpu_count_matches": online_match,
        "cpu_family_counts": families,
        "cpu_family_counts_match": family_counts_match if family_known else None,
        "frequency_groups": groups,
        "frequency_groups_match_target_counts": frequency_counts_match,
        "nominal_frequency_groups_match_within_10_percent": target_freq_match,
        "huawei_y9s_identity_confirmed": bool(identity),
        "note": (
            "ARM CPU part IDs provide family identification when exposed by the kernel. "
            "Frequency-only groups are candidates, not proof of Cortex core identity."
        ),
    }


def median_scores(runs: list[dict[str, Any]]) -> dict[str, Any]:
    by_test: dict[str, list[dict[str, Any]]] = {}
    for run in runs:
        workers = [
            worker for worker in run["workers"]
            if worker.get("affinity_ok") and worker.get("iterations_per_second") is not None
        ]
        if not workers:
            continue
        by_test.setdefault(run["test"], []).append({
            "aggregate_iterations_per_second": sum(
                float(worker["iterations_per_second"]) for worker in workers
            ),
            "per_cpu": {
                str(worker["cpu"]): float(worker["iterations_per_second"])
                for worker in workers
            },
        })
    summary = {}
    for test_name, samples in by_test.items():
        cpu_values: dict[str, list[float]] = {}
        for sample in samples:
            for cpu, value in sample["per_cpu"].items():
                cpu_values.setdefault(cpu, []).append(value)
        summary[test_name] = {
            "median_aggregate_iterations_per_second": round(
                statistics.median(
                    sample["aggregate_iterations_per_second"] for sample in samples
                ), 3
            ),
            "median_per_cpu_iterations_per_second": {
                cpu: round(statistics.median(values), 3)
                for cpu, values in sorted(cpu_values.items(), key=lambda item: int(item[0]))
            },
            "samples": len(samples),
        }
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Profile Android CPU topology and throughput without changing device settings."
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--serial", help="ADB serial; omit for local Android/Termux.")
    mode.add_argument("--local", action="store_true",
                      help="Run directly on Android/Termux instead of using ADB.")
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--profile", type=pathlib.Path,
                        default=pathlib.Path("profiles/huawei-y9s.json"))
    parser.add_argument("--report-dir", type=pathlib.Path, required=True)
    parser.add_argument("--duration-ms", type=int, default=2500)
    parser.add_argument("--repetitions", type=int, default=2)
    args = parser.parse_args()

    if not args.binary.is_file():
        parser.error(f"Benchmark executable not found: {args.binary}")
    if not 1000 <= args.duration_ms <= 10000:
        parser.error("--duration-ms must be between 1000 and 10000")
    if not 1 <= args.repetitions <= 5:
        parser.error("--repetitions must be between 1 and 5")

    reference = read_reference_profile(args.profile)
    target = Target(None if args.local else args.serial)
    report_dir = args.report_dir
    report_dir.mkdir(parents=True, exist_ok=True)

    online_text = target.read_file("/sys/devices/system/cpu/online") or ""
    status_text = target.read_file("/proc/self/status") or ""
    allowed_match = re.search(r"^Cpus_allowed_list:\s*(\S+)", status_text, re.MULTILINE)
    try:
        online_cpus = parse_cpu_list(online_text)
    except ValueError:
        online_cpus = []
    cpuinfo_text = target.read_file("/proc/cpuinfo") or ""
    cpuinfo_records = parse_cpuinfo(cpuinfo_text)
    if not online_cpus:
        online_cpus = sorted({
            int(item["processor"]) for item in cpuinfo_records
            if str(item.get("processor", "")).isdigit()
        })
    if not online_cpus:
        raise RuntimeError("Could not discover online CPU IDs on the Android target.")

    allowed_cpus = parse_cpu_list(allowed_match.group(1)) if allowed_match else online_cpus
    allowed_cpus = sorted(set(online_cpus).intersection(allowed_cpus))
    if not allowed_cpus:
        raise RuntimeError("The current Android process has no online CPUs in its allowed cpuset.")

    core_rows = collect_core_rows(target, online_cpus, cpuinfo_records)
    allowed_core_rows = [row for row in core_rows if row["cpu"] in allowed_cpus]
    grouping = group_cpus(core_rows)
    binary_path = target.install_binary(args.binary)

    test_sets: list[tuple[str, list[int]]] = []
    groups = grouping.get("groups", [])
    high_cpus: list[int] = []
    low_cpus: list[int] = []
    if grouping.get("method") == "arm_cpu_part":
        for group in groups:
            if group["name"] == reference["big_core_family"]:
                high_cpus = group["cpus"]
            elif group["name"] == reference["little_core_family"]:
                low_cpus = group["cpus"]
    elif grouping.get("method") == "cpuinfo_max_freq" and len(groups) >= 2:
        ordered = sorted(groups, key=lambda item: item.get("max_frequency_khz", 0))
        low_cpus, high_cpus = ordered[0]["cpus"], ordered[-1]["cpus"]

    high_allowed = sorted(set(high_cpus).intersection(allowed_cpus))
    low_allowed = sorted(set(low_cpus).intersection(allowed_cpus))
    if high_allowed:
        test_sets.append(("higher_capacity_single_core", [high_allowed[0]]))
    if low_allowed:
        test_sets.append(("lower_capacity_single_core", [low_allowed[0]]))
    test_sets.append(("all_allowed_cores_concurrent", allowed_cpus))

    runs: list[dict[str, Any]] = []
    for repetition in range(1, args.repetitions + 1):
        for test_name, cpu_set in test_sets:
            workers = target.run_benchmark(binary_path, cpu_set, args.duration_ms)
            runs.append({
                "test": test_name,
                "repetition": repetition,
                "requested_cpus": cpu_set,
                "workers": workers,
                "affinity_success": all(row.get("affinity_ok") for row in workers),
            })

    meminfo = target.read_file("/proc/meminfo") or ""
    mem_match = re.search(r"^MemTotal:\s*(\d+)\s*kB", meminfo, re.MULTILINE)
    actual = {
        "manufacturer": target.getprop("ro.product.manufacturer"),
        "model": target.getprop("ro.product.model"),
        "device": target.getprop("ro.product.device"),
        "board": target.getprop("ro.product.board"),
        "hardware": target.getprop("ro.hardware"),
        "soc_model": target.getprop("ro.soc.model") or target.getprop("ro.boot.hardware"),
        "android_release": target.getprop("ro.build.version.release"),
        "android_api_level": target.getprop("ro.build.version.sdk"),
        "primary_abi": target.getprop("ro.product.cpu.abi"),
        "supported_abis": target.getprop("ro.product.cpu.abilist"),
        "memory_total_mb": round(int(mem_match.group(1)) / 1024) if mem_match else None,
        "cpuinfo_records": cpuinfo_records,
        "fingerprint_scope": "local Android/Termux device" if args.local
                            else "ADB-connected Android guest; not automatically physical hardware",
    }
    topology = build_topology_assessment(reference, actual, core_rows, grouping)
    all_affinity = all(run["affinity_success"] for run in runs)
    report = {
        "schema_version": 1,
        "benchmark_id": BENCHMARK_ID,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "execution_mode": "local_android" if args.local else "adb_android_guest",
        "reference_profile": reference,
        "observed_device": actual,
        "cpu_inventory": {
            "online_cpu_list": online_cpus,
            "process_allowed_cpu_list": allowed_cpus,
            "cpuset_restriction_detected": allowed_cpus != online_cpus,
            "cores": core_rows,
            "allowed_cores": allowed_core_rows,
            "grouping": grouping,
        },
        "topology_assessment": topology,
        "benchmarks": {
            "duration_ms": args.duration_ms,
            "repetitions": args.repetitions,
            "runs": runs,
            "median_per_cpu": median_scores(runs),
            "affinity_succeeded_for_all_workers": all_affinity,
        },
        "emulation_assessment": {
            "status": "NOT_EMULATED",
            "cpu_family_identity": "observed from ARM CPU part IDs"
                if grouping.get("method") == "arm_cpu_part"
                else "not confirmed; frequency/capacity groups are approximations",
            "heterogeneous_scheduler_reproduction": False,
            "frequency_scaling_reproduction": False,
            "thermal_behavior_reproduction": False,
            "mali_gpu_reproduction": False,
            "note": (
                "This utility measures a real Android target's CPU topology and integer "
                "throughput. It does not synthesize a Kirin CPU or make a cloud emulator "
                "equivalent to the Huawei Y9s."
            ),
        },
        "calibration_gate": {
            "physical_reference_measurement_required": True,
            "reference_device_confirmed": topology["huawei_y9s_identity_confirmed"],
            "topology_approximately_matches": topology["status"] in (
                "ARM_CPU_FAMILY_TOPOLOGY_MATCH",
                "FREQUENCY_TOPOLOGY_APPROXIMATION_MATCH",
            ),
            "cpu_throttle_applied": False,
            "reason": (
                "No CPU cap is applied until this same benchmark is run on the physical "
                "Huawei Y9s and its throughput envelope is reviewed."
            ),
        },
    }

    json_path = report_dir / "kirin-cpu-profile.json"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# Kirin 710F CPU profile",
        "",
        f"- Benchmark: {BENCHMARK_ID}",
        f"- Execution mode: {report['execution_mode']}",
        f"- Observed device: {actual.get('manufacturer')} {actual.get('model')}",
        f"- SoC reported: {actual.get('soc_model') or actual.get('hardware')}",
        f"- Online CPUs: {','.join(map(str, online_cpus))}",
        f"- CPUs available to profiler process: {','.join(map(str, allowed_cpus))}",
        f"- Topology assessment: **{topology['status']}**",
        f"- Huawei Y9s identity confirmed: **{topology['huawei_y9s_identity_confirmed']}**",
        f"- CPU affinity successful: **{all_affinity}**",
        "- CPU emulation: **NOT EMULATED**",
        "- CPU throttle applied: **NO**",
        "",
        "## Observed cores",
        "",
        "| CPU | Family | Max MHz | Current MHz | Capacity | Cluster ID |",
        "|---:|---|---:|---:|---:|---:|",
    ]
    for row in core_rows:
        max_mhz = row["cpuinfo_max_freq_khz"] / 1000 if row.get("cpuinfo_max_freq_khz") else None
        cur_mhz = row["scaling_cur_freq_khz"] / 1000 if row.get("scaling_cur_freq_khz") else None
        lines.append(
            f"| {row['cpu']} | {row.get('cpu_family')} | "
            f"{max_mhz if max_mhz is not None else 'n/a'} | "
            f"{cur_mhz if cur_mhz is not None else 'n/a'} | "
            f"{row.get('cpu_capacity') if row.get('cpu_capacity') is not None else 'n/a'} | "
            f"{row.get('cluster_id') if row.get('cluster_id') is not None else 'n/a'} |"
        )
    lines += [
        "",
        "## Benchmark throughput",
        "",
        "| Test | Repetition | CPU IDs | Aggregate iterations/s |",
        "|---|---:|---|---:|",
    ]
    for run in runs:
        rates = [row["iterations_per_second"] for row in run["workers"]
                 if row.get("affinity_ok") and row.get("iterations_per_second") is not None]
        total = round(sum(rates), 3) if rates else "n/a"
        lines.append(
            f"| {run['test']} | {run['repetition']} | "
            f"{','.join(map(str, run['requested_cpus']))} | {total} |"
        )
    lines += [
        "",
        "## Interpretation",
        "",
        "This is a CPU calibration sample, not a Geometry Dash performance result. "
        "Frequency-derived clusters are candidates; ARM CPU part IDs are stronger family evidence.",
        "",
        "No CPU frequency, governor, scheduler policy, or system CPU limit was changed. "
        "Do not label cloud results as Huawei Y9s results or derive a throttle from this "
        "report until a local measurement from the physical Y9s is reviewed.",
        "",
    ]
    (report_dir / "kirin-cpu-profile.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"CPU profile: {json_path}")
    print(f"Summary: {report_dir / 'kirin-cpu-profile.md'}")
    print(f"Topology: {topology['status']}; emulation: NOT_EMULATED")
    return 0 if all_affinity else 3


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
