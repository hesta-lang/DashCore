#!/usr/bin/env python3
"""Measure Geometry Dash frame timing first without DashCore, then with DashCore."""
from __future__ import annotations
import argparse, csv, json, math, pathlib, re, shlex, statistics, subprocess, sys, time
from PIL import Image
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from kirin_autotest_calibration import build_physical_cpu_reference

def load_device_profile(path: pathlib.Path) -> dict[str, Any]:
    """Load and validate a reference-device profile without changing emulator settings."""
    if not path.is_file():
        raise FileNotFoundError(f"Device profile not found: {path}")
    profile = json.loads(path.read_text(encoding="utf-8"))
    required = ("schema_version", "profile_id", "display_name", "device", "android", "display", "benchmark")
    missing = [key for key in required if key not in profile]
    if missing:
        raise ValueError(f"Device profile is missing fields: {', '.join(missing)}")
    if profile["schema_version"] != 1:
        raise ValueError(f"Unsupported device profile schema: {profile['schema_version']!r}")
    if not profile["profile_id"] or not profile["display_name"]:
        raise ValueError("Device profile ID and display name must be non-empty")
    device, android, display = profile["device"], profile["android"], profile["display"]
    if not android.get("api_level") or not android.get("abi"):
        raise ValueError("Device profile must define Android API level and ABI")
    if not display.get("width_px") or not display.get("height_px"):
        raise ValueError("Device profile must define display width and height")
    if not device.get("gpu", {}).get("name"):
        raise ValueError("Device profile must define the reference GPU name")
    return profile


def collect_device_fingerprint(serial: str) -> dict[str, Any]:
    """Record the cloud guest as it actually exists; never apply reference-device overrides."""
    def prop(name: str) -> str:
        return shell(serial, f"getprop {shlex.quote(name)}", check=False, timeout=30).strip()

    wm_size = shell(serial, "wm size", check=False, timeout=30).strip()
    wm_density = shell(serial, "wm density", check=False, timeout=30).strip()
    display_dump = shell(serial, "dumpsys display", check=False, timeout=45)
    sf_dump = shell(serial, "dumpsys SurfaceFlinger", check=False, timeout=45)
    cpuinfo = shell(serial, "cat /proc/cpuinfo", check=False, timeout=30)
    meminfo = shell(serial, "cat /proc/meminfo", check=False, timeout=30)
    size_matches = re.findall(r"(?:Override|Physical) size:\s*(\d+)x(\d+)", wm_size)
    density_matches = re.findall(r"(?:Override|Physical) density:\s*(\d+)", wm_density)
    refresh_match = re.search(r"(?:refreshRate|mRefreshRate|fps)[=: ]+([0-9]+(?:\.[0-9]+)?)",
                              display_dump, re.IGNORECASE)
    renderer_match = re.search(r"(?:GLES:\s*|OpenGL ES\s+)([^\r\n]+)", sf_dump, re.IGNORECASE)
    mem_match = re.search(r"^MemTotal:\s*(\d+)\s*kB", meminfo, re.MULTILINE)
    cpu_model_match = None
    # Emulators may expose "Processor: 0", which is a core index, not a CPU model.
    for cpu_key in ("model name", "Hardware", "Processor"):
        for candidate in re.finditer(
            rf"^{re.escape(cpu_key)}\s*:\s*(.+)$",
            cpuinfo,
            re.MULTILINE | re.IGNORECASE
        ):
            value = candidate.group(1).strip()
            if value and not value.isdigit():
                cpu_model_match = candidate
                break
        if cpu_model_match:
            break
    logical_cores = len(re.findall(r"^processor\s*:", cpuinfo, re.MULTILINE))
    return {
        "manufacturer": prop("ro.product.manufacturer"),
        "model": prop("ro.product.model"),
        "device": prop("ro.product.device"),
        "board": prop("ro.product.board"),
        "hardware": prop("ro.hardware"),
        "soc_model": prop("ro.soc.model") or prop("ro.boot.hardware"),
        "android_release": prop("ro.build.version.release"),
        "android_api_level": prop("ro.build.version.sdk"),
        "primary_abi": prop("ro.product.cpu.abi"),
        "supported_abis": prop("ro.product.cpu.abilist"),
        "native_bridge": prop("ro.dalvik.vm.native.bridge"),
        "opengles_version_property": prop("ro.opengles.version"),
        "egl_implementation": prop("ro.hardware.egl"),
        "wm_size_raw": wm_size,
        "wm_size_reported_px": [int(size_matches[-1][0]), int(size_matches[-1][1])] if size_matches else None,
        "wm_density_raw": wm_density,
        "wm_density_dpi": int(density_matches[-1]) if density_matches else None,
        "display_refresh_rate_hz": float(refresh_match.group(1)) if refresh_match else None,
        "surfaceflinger_renderer": renderer_match.group(1).strip() if renderer_match else None,
        "cpu_model_reported": cpu_model_match.group(1).strip() if cpu_model_match else None,
        "cpu_logical_cores": logical_cores or None,
        "memory_total_mb": round(int(mem_match.group(1)) / 1024) if mem_match else None,
        "fingerprint_scope": "cloud Android guest only; host GPU/CPU and physical phone thermals are not reproduced"
    }


def assess_device_profile(profile: dict[str, Any], actual: dict[str, Any]) -> dict[str, Any]:
    """Compare observable guest facts with the reference profile, with explicit limits."""
    target_android = profile["android"]
    target_display = profile["display"]
    actual_api = int(actual["android_api_level"]) if str(actual.get("android_api_level", "")).isdigit() else None
    actual_abis = [v.strip() for v in actual.get("supported_abis", "").split(",") if v.strip()]
    actual_size = actual.get("wm_size_reported_px")
    target_size = [int(target_display["width_px"]), int(target_display["height_px"])]
    normalized_target = sorted(target_size, reverse=True)
    normalized_actual = sorted(actual_size, reverse=True) if actual_size else None
    api_status = ("MATCH" if actual_api == int(target_android["api_level"]) else
                  "DIFFERENT" if actual_api is not None else "UNKNOWN")
    abi_status = ("ARM64_ABI_AVAILABLE" if target_android["abi"] in actual_abis or
                  actual.get("primary_abi") == target_android["abi"] else "NOT_CONFIRMED")
    resolution_status = ("MATCH" if normalized_actual == normalized_target else
                         "DIFFERENT" if normalized_actual else "UNKNOWN")
    return {
        "profile_id": profile["profile_id"],
        "display_name": profile["display_name"],
        "role": "reference_hardware_profile",
        "execution_environment": "cloud_android_emulator",
        "hardware_equivalence": "NOT_EMULATED",
        "target": {
            "manufacturer": profile["device"].get("manufacturer"),
            "model": profile["device"].get("model"),
            "soc": profile["device"].get("soc", {}).get("name"),
            "cpu_topology": profile["device"].get("soc", {}).get("cpu_topology"),
            "gpu": profile["device"].get("gpu", {}).get("name"),
            "physical_ram_mb": profile["device"].get("memory", {}).get("physical_ram_mb"),
            "android_release": target_android.get("version"),
            "android_api_level": target_android.get("api_level"),
            "abi": target_android.get("abi"),
            "display_px": target_size,
            "refresh_rate_hz": target_display.get("refresh_rate_hz")
        },
        "observed_cloud_guest": actual,
        "comparison": {
            "android_api": api_status,
            "arm64_abi": abi_status,
            "display_resolution": resolution_status,
            "guest_memory_total_mb": actual.get("memory_total_mb"),
            "guest_cpu_logical_cores": actual.get("cpu_logical_cores")
        },
        "cpu_calibration_reference": build_physical_cpu_reference(profile),
        "limitations": [
            "The cloud Pixel emulator is not a Kirin 710F CPU or Mali-G51 MP4 GPU.",
            "Cloud CPU scheduling, GPU drivers, memory bandwidth, thermals and physical frame pacing differ from the Huawei Y9s.",
            "Reference values are descriptive metadata only; no resolution, density, ABI, GPU or CPU override is applied."
        ]
    }



def run_adb(serial: str, *args: str, input_text: str | None = None, timeout: int = 90, check: bool = True) -> str:
    proc = subprocess.run(["adb", "-s", serial, *args], input=input_text, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if check and proc.returncode != 0:
        raise RuntimeError(f"adb {' '.join(args)} failed ({proc.returncode}): {proc.stdout[-3000:]}")
    return proc.stdout

def shell(serial: str, command: str, check: bool = True, timeout: int = 90) -> str:
    return run_adb(serial, "shell", command, timeout=timeout, check=check)

def pull(serial: str, remote: str, local: pathlib.Path) -> bool:
    local.parent.mkdir(parents=True, exist_ok=True)
    result = run_adb(serial, "pull", remote, str(local), check=False, timeout=120)
    return "file pulled" in result or "files pulled" in result or local.exists()

def screenshot(serial: str, remote_name: str, destination: pathlib.Path) -> None:
    remote = f"/sdcard/{remote_name}"
    shell(serial, f"screencap -p {shlex.quote(remote)}", check=False)
    if not pull(serial, remote, destination):
        raise RuntimeError(f"Could not retrieve screenshot {remote_name}")

def read_remote_logs(serial: str, log_dir: str, dest: pathlib.Path) -> str:
    dest.mkdir(parents=True, exist_ok=True)
    run_adb(serial, "pull", log_dir, str(dest), check=False, timeout=120)
    parts = []
    for path in sorted(dest.rglob("*")):
        if path.is_file():
            try: parts.append(path.read_text(errors="replace"))
            except OSError: pass
    return "\n".join(parts)

def process_ticks(stat_line: str) -> int | None:
    try:
        tail = stat_line.split(")", 1)[1].split()
        return int(tail[11]) + int(tail[12])
    except (IndexError, ValueError): return None

def percentile(values: list[float], p: float) -> float | None:
    if not values: return None
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, max(0, math.ceil(p*len(ordered))-1))]

def parse_gfxinfo(path: pathlib.Path) -> dict[str, Any]:
    if not path.exists(): return {"available": False, "reason": "gfxinfo file missing"}
    lines = path.read_text(errors="replace").splitlines()
    completion_times = []
    for index, line in enumerate(lines):
        if "FrameCompleted" not in line or "IntendedVsync" not in line: continue
        header = next(csv.reader([line]))
        try: col = header.index("FrameCompleted")
        except ValueError: continue
        for row in csv.reader(lines[index+1:]):
            if len(row) <= col: continue
            try: stamp = int(row[col].strip())
            except (ValueError, IndexError): continue
            if stamp > 0: completion_times.append(stamp)
        break
    intervals = [(right-left)/1_000_000.0 for left, right in zip(completion_times, completion_times[1:]) if right > left]
    if not intervals:
        return {"available": False, "reason": "No FrameCompleted timestamps parsed"}
    mean_ms, min_ms, max_ms = statistics.mean(intervals), min(intervals), max(intervals)
    return {
        "available": True,
        "interval_count": len(intervals),
        "max_fps": 1000.0 / min_ms,
        "average_fps": 1000.0 / mean_ms,
        "min_fps": 1000.0 / max_ms,
        "mean_frame_ms": mean_ms,
        "fastest_frame_ms": min_ms,
        "slowest_frame_ms": max_ms,
        "p95_frame_ms": percentile(intervals, .95),
        "over_50ms": sum(value > 50.0 for value in intervals),
        "over_100ms": sum(value > 100.0 for value in intervals),
    }

def parse_surfaceflinger_latency(text: str) -> dict[str, Any]:
    """Parse SurfaceFlinger presentation timestamps without imposing a refresh-rate FPS cap."""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    refresh_ns = None
    if lines:
        try: refresh_ns = int(lines[0].split()[0])
        except (ValueError, IndexError): pass
    present_ns = []
    for line in lines[1:]:
        fields = line.split()
        if len(fields) < 2: continue
        try:
            stamp = int(fields[1])  # actualPresentTime
        except ValueError:
            continue
        if 0 < stamp < (2**63 - 1):
            present_ns.append(stamp)
    present_ns = sorted(set(present_ns))
    raw_intervals = [(b-a)/1_000_000.0 for a, b in zip(present_ns, present_ns[1:])
                     if b > a and 0.1 <= (b-a)/1_000_000.0 <= 1000.0]
    # Surface presentation intervals are not capped by the panel refresh rate.
    # Keep sub-refresh intervals: they may represent valid unlocked frame production.
    anomalous = []
    intervals = raw_intervals
    anomaly_fields = {
        "raw_interval_count": len(raw_intervals),
        "timing_anomaly_count": len(anomalous),
        "timing_anomaly_detected": bool(anomalous),
        "timing_anomaly_threshold_ms": None,
    }
    if len(intervals) < 10:
        return {"available": False,
                "reason": f"Insufficient plausible SurfaceFlinger frames ({len(intervals)} intervals; {len(anomalous)} timing anomalies)",
                "frame_count": len(present_ns), "refresh_period_ns": refresh_ns,
                **anomaly_fields}
    mean_ms, min_ms, max_ms = statistics.mean(intervals), min(intervals), max(intervals)
    return {
        "available": True, "source": "SurfaceFlinger actualPresentTime",
        "frame_count": len(present_ns), "interval_count": len(intervals),
        "refresh_period_ns": refresh_ns,
        "max_fps": 1000.0 / min_ms, "average_fps": 1000.0 / mean_ms,
        "min_fps": 1000.0 / max_ms, "mean_frame_ms": mean_ms,
        "fastest_frame_ms": min_ms, "slowest_frame_ms": max_ms,
        "p95_frame_ms": percentile(intervals, .95),
        "over_50ms": sum(value > 50.0 for value in intervals),
        "over_100ms": sum(value > 100.0 for value in intervals),
        **anomaly_fields,
    }

def prepare_surfaceflinger(serial: str, mode_dir: pathlib.Path) -> list[str]:
    listing = shell(serial, "dumpsys SurfaceFlinger --list", check=False, timeout=60)
    (mode_dir / "surfaceflinger-layers.txt").write_text(listing)
    candidates = []
    for line in listing.splitlines():
        name = line.strip()
        if not name: continue
        low = name.lower()
        if "com.geode.launcher" in low or "geometrydashactivity" in low:
            candidates.append(name)
    # Keep the actual app-owned layers; exclude unrelated launcher/system layers.
    candidates = list(dict.fromkeys(candidates))
    (mode_dir / "surfaceflinger-candidates.txt").write_text("\n".join(candidates) + ("\n" if candidates else ""))
    for index, name in enumerate(candidates):
        shell(serial, "dumpsys SurfaceFlinger --latency-clear " + shlex.quote(name), check=False, timeout=30)
    return candidates

def collect_surfaceflinger(serial: str, candidates: list[str], mode_dir: pathlib.Path) -> dict[str, Any]:
    best: dict[str, Any] = {"available": False, "reason": "No app-owned SurfaceFlinger layer found"}
    for index, name in enumerate(candidates):
        raw = shell(serial, "dumpsys SurfaceFlinger --latency " + shlex.quote(name), check=False, timeout=60)
        (mode_dir / f"surfaceflinger-latency-{index}.txt").write_text("LAYER=" + name + "\n" + raw)
        parsed = parse_surfaceflinger_latency(raw)
        parsed["layer"] = name
        if parsed.get("available"):
            if not best.get("available") or parsed.get("frame_count", 0) > best.get("frame_count", 0):
                best = parsed
        elif not best.get("available") and parsed.get("frame_count", 0) > best.get("frame_count", 0):
            best = parsed
    return best

def parse_update(log_text: str) -> dict[str, int] | None:
    pattern = re.compile(r"UPDATE FINAL: frames=(\d+), avg=(\d+)us, max=(\d+)us, over8ms=(\d+), callbackGap>50ms=(\d+), callbackGap>100ms=(\d+), maxGap=(\d+)us")
    match = pattern.search(log_text)
    if not match: return None
    keys = ("frames", "avg_us", "max_us", "over8ms", "gaps_over_50ms", "gaps_over_100ms", "max_gap_us")
    return dict(zip(keys, map(int, match.groups())))

def run_device_observation(serial: str, pid: str, mode_dir: pathlib.Path) -> dict[str, Any]:
    script = r"""PID="$1"
CLK_TCK="$(getconf CLK_TCK 2>/dev/null || echo 100)"
read U0 _ < /proc/uptime
STAT_BEFORE="$(cat "/proc/$PID/stat" 2>/dev/null || true)"
START="$U0"
COUNT=0
while :; do
  read NOW _ < /proc/uptime
  awk -v a="$START" -v b="$NOW" 'BEGIN { exit !((b-a) < 20.0) }' || break
  input tap 700 800 >/dev/null 2>&1 || true
  COUNT=$((COUNT+1))
  sleep 0.5
done
read END _ < /proc/uptime
STAT_AFTER="$(cat "/proc/$PID/stat" 2>/dev/null || true)"
echo "DEVICE_START=$START"
echo "DEVICE_END=$END"
echo "TAP_COUNT=$COUNT"
echo "CLK_TCK=$CLK_TCK"
echo "STAT_BEFORE=$STAT_BEFORE"
echo "STAT_AFTER=$STAT_AFTER"
"""
    proc = subprocess.run(["adb", "-s", serial, "shell", "sh", "-s", pid], input=script, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
    (mode_dir / "device-observation.txt").write_text(proc.stdout)
    if proc.returncode != 0: return {"available": False, "reason": f"device sample exited {proc.returncode}"}
    markers = {}
    for line in proc.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1); markers[key] = value
    try:
        elapsed = float(markers["DEVICE_END"]) - float(markers["DEVICE_START"])
        hz = float(markers.get("CLK_TCK", "100"))
        before, after = process_ticks(markers["STAT_BEFORE"]), process_ticks(markers["STAT_AFTER"])
        if elapsed <= 0 or hz <= 0 or before is None or after is None: raise ValueError("invalid process samples")
        delta = max(0, after-before); cpu_seconds = delta/hz
        return {"available": True, "elapsed_s": elapsed, "cpu_ticks": delta, "clk_tck": hz,
                "cpu_seconds": cpu_seconds, "cpu_percent_one_core": cpu_seconds/elapsed*100.0,
                "tap_count": int(markers.get("TAP_COUNT", "0"))}
    except (KeyError, ValueError): return {"available": False, "reason": "Could not parse device process samples"}

def screenshot_text(path: pathlib.Path) -> str:
    """OCR a screenshot; tesseract is installed by the workflow."""
    try:
        proc = subprocess.run(["tesseract", str(path), "stdout", "--psm", "6"],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=20)
        return proc.stdout.casefold() if proc.returncode == 0 else ""
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return ""

def take_state_screenshot(serial: str, mode: str, label: str, destination: pathlib.Path) -> str:
    screenshot(serial, f"dashcore-{mode.lower()}-{label}.png", destination)
    text = screenshot_text(destination)
    (destination.parent / f"{label}-ocr.txt").write_text(text + "\n")
    return text

def wait_for_state(serial: str, mode: str, label: str, destination: pathlib.Path,
                   predicate, timeout_s: int = 60, interval_s: float = 2.0) -> str:
    deadline = time.monotonic() + timeout_s
    last_text = ""
    attempt = 0
    while time.monotonic() < deadline:
        attempt += 1
        last_text = take_state_screenshot(serial, mode, f"{label}-{attempt}", destination)
        # NOTICE can reappear between app launches (including after the main
        # menu was already confirmed). Handle it inside every state wait, not
        # only during startup, so later culling-ON repetitions cannot stall
        # on the Terms of Use modal while waiting for level-select/gameplay.
        if "notice" in last_text or "terms of use" in last_text:
            update_notice_state(destination.parent, dialog_detected=True)
            wait_for_notice_dismissal(serial, mode, destination.parent)
            time.sleep(1.0)
            continue
        if predicate(last_text):
            # The first OCR match is already a valid state observation. A second
            # OCR pass can miss transient HUD text even when the game state has
            # not changed; never discard a positive detection for that reason.
            confirmed_path = destination.with_name(f"{label}-confirmed.png")
            screenshot(
                serial,
                f"dashcore-{mode.lower()}-{label}-confirmed.png",
                confirmed_path,
            )
            confirmed_text = screenshot_text(confirmed_path)
            accepted_text = confirmed_text if predicate(confirmed_text) else last_text
            (destination.parent / f"{label}-confirmed-ocr.txt").write_text(
                accepted_text + "\n"
            )
            (destination.parent / f"{label}-confirmation.json").write_text(
                json.dumps({
                    "initial_detection": True,
                    "confirmation_ocr_match": bool(predicate(confirmed_text)),
                    "accepted_source": "confirmation" if predicate(confirmed_text) else "initial_detection",
                }, indent=2) + "\n"
            )
            return accepted_text
        time.sleep(interval_s)
    raise RuntimeError(f"{mode}: timed out waiting for {label}; last OCR text: {last_text[:500]!r}")

def detect_notice_ok_button(frame: Image.Image) -> tuple[int, int] | None:
    """Find the bright-green OK button in a Geometry Dash NOTICE screenshot."""
    image = frame.convert("RGB")
    width, height = image.size
    pixels = image.load()
    x0, x1 = int(width * 0.40), int(width * 0.60)
    y0, y1 = int(height * 0.55), int(height * 0.90)
    minimum_row_hits = max(8, int((x1 - x0) * 0.02))
    row_hits: dict[int, list[int]] = {}

    for y in range(y0, y1):
        hits = []
        for x in range(x0, x1):
            red, green, blue = pixels[x, y]
            if green >= 145 and green > red * 1.20 and green > blue * 1.20:
                hits.append(x)
        if len(hits) >= minimum_row_hits:
            row_hits[y] = hits

    if not row_hits:
        return None

    # Join adjacent green rows into candidate button regions. The modal's
    # highlighted OK button is the largest bright-green component in this ROI.
    groups: list[list[int]] = []
    current: list[int] = []
    previous_y: int | None = None
    for y in sorted(row_hits):
        if previous_y is None or y - previous_y <= 2:
            current.append(y)
        else:
            groups.append(current)
            current = [y]
        previous_y = y
    if current:
        groups.append(current)

    candidates = []
    for rows in groups:
        xs = [x for row in rows for x in row_hits[row]]
        left, right = min(xs), max(xs)
        top, bottom = min(rows), max(rows)
        box_width, box_height = right - left + 1, bottom - top + 1
        center_x = (left + right) // 2
        center_y = (top + bottom) // 2
        if not (width * 0.42 <= center_x <= width * 0.58):
            continue
        if not (width * 0.04 <= box_width <= width * 0.16):
            continue
        if not (height * 0.025 <= box_height <= height * 0.13):
            continue
        if not (height * 0.58 <= center_y <= height * 0.88):
            continue
        peak = max(len(row_hits[row]) for row in rows)
        candidates.append((peak * 1000 + len(xs), center_x, center_y))

    if not candidates:
        return None
    _, center_x, center_y = max(candidates)
    return center_x, center_y


def record_notice_tap(variant_dir: pathlib.Path, event: dict[str, Any]) -> None:
    path = variant_dir / "notice-handling.json"
    try:
        state = json.loads(path.read_text()) if path.exists() else {}
    except (OSError, json.JSONDecodeError):
        state = {}
    taps = state.get("taps", [])
    taps.append(event)
    state.update({"dialog_detected": True, "taps": taps})
    path.write_text(json.dumps(state, indent=2) + "\n")


def update_notice_state(variant_dir: pathlib.Path, **changes: Any) -> None:
    path = variant_dir / "notice-handling.json"
    try:
        state = json.loads(path.read_text()) if path.exists() else {}
    except (OSError, json.JSONDecodeError):
        state = {}
    state.update(changes)
    path.write_text(json.dumps(state, indent=2) + "\n")


def parse_wm_size(text: str, fallback_width: int, fallback_height: int) -> tuple[int, int]:
    """Read the configured display size, falling back to captured frame size."""
    sizes = re.findall(r"(?:Override|Physical) size:\s*(\d+)x(\d+)", text)
    if sizes:
        return tuple(map(int, sizes[-1]))
    return fallback_width, fallback_height


def map_screenshot_point_to_input(
    screen_x: int, screen_y: int,
    screenshot_width: int, screenshot_height: int,
    wm_width: int, wm_height: int,
) -> tuple[int, int, int, int]:
    """Map screenshot pixels into ADB input coordinates for the current orientation.

    `wm size` can report portrait panel dimensions while an app is running in
    landscape. ADB input coordinates follow the current display orientation,
    as does the captured game frame.
    """
    if min(screenshot_width, screenshot_height, wm_width, wm_height) <= 0:
        raise ValueError("Display and screenshot dimensions must be positive")
    input_width, input_height = wm_width, wm_height
    if (screenshot_width > screenshot_height) != (wm_width > wm_height):
        input_width, input_height = wm_height, wm_width
    x = round(screen_x * input_width / screenshot_width)
    y = round(screen_y * input_height / screenshot_height)
    return x, y, input_width, input_height


def tap_notice_confirmation(serial: str, screenshot_path: pathlib.Path) -> tuple[int, int]:
    with Image.open(screenshot_path) as frame:
        width, height = frame.size
        detected = detect_notice_ok_button(frame)
    if detected is None:
        # Measured fallback for this landscape Geometry Dash NOTICE layout.
        screen_x, screen_y = round(width * 0.50), round(height * 0.734)
        method = "measured-layout-fallback"
    else:
        screen_x, screen_y = detected
        method = "green-button-pixel-detection"

    # wm size may report the panel's portrait dimensions while the game and
    # ADB input coordinate space are landscape. Match dimensions to the
    # screenshot orientation before scaling; do not rotate the point.
    display_text = shell(serial, "wm size", check=False)
    display_width, display_height = parse_wm_size(display_text, width, height)
    x, y, input_width, input_height = map_screenshot_point_to_input(
        screen_x, screen_y, width, height, display_width, display_height
    )
    if (input_width, input_height) != (display_width, display_height):
        method += "+current-orientation-input-space"
    else:
        method += "+scaled-input-space"

    shell(serial, f"input tap {x} {y}", check=False)

    record_notice_tap(screenshot_path.parent, {
        "method": method,
        "screenshot_size": [width, height],
        "wm_size_reported": [display_width, display_height],
        "input_coordinate_size": [input_width, input_height],
        "button_center_screenshot_px": [screen_x, screen_y],
        "adb_tap_px": [x, y],
    })
    print(
        f"NOTICE_CONFIRMATION: method={method} screenshot={width}x{height} "
        f"wm_size={display_width}x{display_height} "
        f"input_space={input_width}x{input_height} tap={x},{y}"
    )
    return x, y

def wait_for_notice_dismissal(serial: str, mode: str, variant_dir: pathlib.Path) -> None:
    deadline = time.monotonic() + 60
    attempt = 0
    while time.monotonic() < deadline:
        attempt += 1
        path = variant_dir / f"notice-poll-{attempt}.png"
        text = take_state_screenshot(serial, mode, f"notice-poll-{attempt}", path)
        if "notice" not in text and "terms of use" not in text:
            update_notice_state(variant_dir, dismissed=True, dismissal_poll=attempt)
            return
        tap_notice_confirmation(serial, path)
        time.sleep(2.0)
    raise RuntimeError(f"{mode}: NOTICE / Terms of Use dialog did not close after automatic button-detection taps.")

def is_gameplay_text(ocr_text: str) -> bool:
    """Recognize active gameplay by its ATTEMPT HUD, not level-select percentages."""
    return bool(re.search(r"\b(?:attempt|apt)\s*#?\s*\d+\b", ocr_text, re.IGNORECASE))


def is_main_menu_text(ocr_text: str) -> bool:
    """Recognize the GD main menu without depending on OCR of its logo."""
    normalized = " ".join(ocr_text.lower().split())
    menu_landmarks = "character select" in normalized and "level editor" in normalized
    legacy_menu_match = (
        "play" in normalized and
        ("geometry dash" in normalized or "settings" in normalized or "editor" in normalized)
    )
    return menu_landmarks or legacy_menu_match


def wait_for_main_menu(serial: str, mode: str, variant_dir: pathlib.Path,
                       timeout_s: int = 90) -> str:
    # The notice may appear after the Geode updater splash, not in the first
    # startup screenshot. Detect and dismiss it while polling for the menu.
    deadline = time.monotonic() + timeout_s
    attempt = 0
    last_text = ""
    while time.monotonic() < deadline:
        attempt += 1
        path = variant_dir / f"main-menu-poll-{attempt}.png"
        last_text = take_state_screenshot(serial, mode, f"main-menu-poll-{attempt}", path)
        if "notice" in last_text or "terms of use" in last_text:
            update_notice_state(variant_dir, dialog_detected=True)
            tap_notice_confirmation(serial, path)
            time.sleep(2.0)
            continue
        if is_main_menu_text(last_text):
            screenshot(serial, f"dashcore-{mode.lower()}-main-menu-confirmed.png",
                       variant_dir / "main-menu-confirmed.png")
            update_notice_state(
                variant_dir,
                main_menu_confirmed=True,
                notice_status="dismissed" if (variant_dir / "notice-handling.json").exists() else "not-shown",
            )
            return last_text
        time.sleep(2.0)
    raise RuntimeError(f"{mode}: timed out waiting for main menu after handling NOTICE; last OCR text: {last_text[:500]!r}")

def parse_culling_runtime(log_text: str) -> dict[str, Any]:
    """Parse culling activation, sample totals, per-reason counts and nearest candidate."""
    activations = re.findall(
        r"static-culling-requested=(true|false),\s*static-culling-active=(true|false)",
        log_text, re.IGNORECASE
    )
    samples = re.findall(
        r"CULL-STATS frames=(\d+),\s*warmupFrames=(\d+),\s*visitedGameObjects=(\d+),\s*eligibleObjects=(\d+),\s*culledObjects=(\d+)",
        log_text, re.IGNORECASE
    )
    reason_keys = (
        "visited", "eligible", "protected_effect", "protected_trigger",
        "protected_grouped", "protected_rotating", "missing_director",
        "invalid_content_size", "invalid_anchor", "invalid_world_bounds",
        "invalid_viewport", "within_guard_band", "offscreen_x",
        "offscreen_y", "offscreen_both",
    )
    reason_pattern = (
        r"CULL-REASONS visited=(\d+),\s*eligible=(\d+),"
        r"\s*protectedEffect=(\d+),\s*protectedTrigger=(\d+),"
        r"\s*protectedGrouped=(\d+),\s*protectedRotating=(\d+),"
        r"\s*missingDirector=(\d+),\s*contentSizeFallbacks=(\d+),"
        r"\s*invalidBoundingRect=(\d+),\s*invalidAnchor=(\d+),\s*invalidWorldBounds=(\d+),"
        r"\s*invalidViewport=(\d+),\s*withinGuardBand=(\d+),"
        r"\s*offscreenX=(\d+),\s*offscreenY=(\d+),\s*offscreenBoth=(\d+)"
    )
    reason_samples = re.findall(reason_pattern, log_text, re.IGNORECASE)
    nearest_samples = re.findall(
        r"CULL-NEAREST: found=(true|false),\s*guardClearance=(-?[\d.]+),"
        r"\s*objectID=(-?\d+),\s*bounds=\(([-\d.]+),([-\d.]+)\)-\(([-\d.]+),([-\d.]+)\)",
        log_text, re.IGNORECASE
    )

    requested, active = activations[-1] if activations else ("false", "false")
    frames, warmup_frames, visited, eligible, culled = (
        map(int, samples[-1]) if samples else (0, 0, 0, 0, 0)
    )
    reasons = dict(zip(reason_keys, map(int, reason_samples[-1]))) if reason_samples else {
        key: 0 for key in reason_keys
    }
    protected_total = sum(reasons[key] for key in (
        "protected_effect", "protected_trigger", "protected_grouped", "protected_rotating"
    ))
    bounds_total = sum(reasons[key] for key in (
        "missing_director", "invalid_bounding_rect", "invalid_anchor",
        "invalid_world_bounds", "invalid_viewport", "within_guard_band",
        "offscreen_x", "offscreen_y", "offscreen_both"
    ))
    observed_culled = reasons["offscreen_x"] + reasons["offscreen_y"] + reasons["offscreen_both"]
    reason_totals_match = bool(reason_samples and samples) and (
        reasons["visited"] == visited
        and reasons["eligible"] == eligible
        and protected_total + bounds_total == visited
        and bounds_total == eligible
        and observed_culled == culled
    )
    nearest = None
    if nearest_samples:
        found, clearance, object_id, min_x, min_y, max_x, max_y = nearest_samples[-1]
        nearest = {
            "found": found.lower() == "true",
            "guard_clearance": float(clearance),
            "object_id": int(object_id),
            "bounds": [float(min_x), float(min_y), float(max_x), float(max_y)],
        }
    return {
        "activation_report_found": bool(activations),
        "requested": requested.lower() == "true",
        "active": active.lower() == "true",
        "active_confirmed": bool(activations) and active.lower() == "true",
        "stats_sample_count": len(samples),
        "frames": frames,
        "warmup_frames": warmup_frames,
        "visited_game_objects": visited,
        "eligible_objects": eligible,
        "culled_objects": culled,
        "actual_culling_observed": culled > 0,
        "reason_report_found": bool(reason_samples),
        "reason_counts": reasons,
        "protected_objects_total": protected_total,
        "eligible_reason_total": bounds_total,
        "reason_culled_total": observed_culled,
        "reason_totals_match": reason_totals_match,
        "nearest_guard_band_candidate": nearest,
    }


def run_variant(args: argparse.Namespace, mode: str, mod_enabled: bool, culling_mode: str = "off") -> dict[str, Any]:
    serial, base = args.serial, args.base_dir.rstrip("/")
    mod_dir = f"{base}/game/geode/mods"
    mod_remote = f"{mod_dir}/DashCore.geode"
    config_dir = f"{base}/game/geode/config/{args.mod_id}"
    culling_mode_remote = f"{config_dir}/static-culling-mode.txt"
    log_dir = f"{base}/game/geode/logs"
    variant_dir = pathlib.Path(args.report_dir) / mode.lower().replace("_", "-")
    variant_dir.mkdir(parents=True, exist_ok=True)

    shell(serial, "am force-stop com.geode.launcher", check=False); time.sleep(2)
    shell(serial, f"mkdir -p {shlex.quote(mod_dir)}")
    shell(serial, f"rm -f {shlex.quote(mod_remote)}")
    if mod_enabled:
        if culling_mode not in ("on", "off"):
            raise ValueError(f"Unsupported culling mode: {culling_mode!r}")
        run_adb(serial, "push", args.mod_file, mod_remote, timeout=120)
        shell(serial, f"test -s {shlex.quote(mod_remote)}")
        shell(serial, f"mkdir -p {shlex.quote(config_dir)}")
        shell(serial, f"printf '%s\\n' {shlex.quote(culling_mode)} > {shlex.quote(culling_mode_remote)}")
    else:
        shell(serial, f"rm -f {shlex.quote(culling_mode_remote)}")
    shell(serial, f"mkdir -p {shlex.quote(log_dir)} && rm -f {shlex.quote(log_dir)}/*")
    shell(serial, "logcat -c", check=False)
    shell(serial, "am start -a android.intent.action.VIEW -d geode-launcher://main/launch -p com.geode.launcher", check=False)

    startup_text = wait_for_state(
        serial, mode, "startup", variant_dir / "startup.png",
        lambda t: bool(t.strip()), timeout_s=75
    )
    if "notice" in startup_text or "terms of use" in startup_text:
        wait_for_notice_dismissal(serial, mode, variant_dir)

    # Geometry Dash's level-select screen also contains "0%" progress labels.
    # The in-level HUD instead exposes the "ATTEMPT N" label; use that to avoid
    # treating the Stereo Madness selection screen as active gameplay.
    gameplay_pattern = re.compile(r"\battempt\s*#?\s*\d+\b", re.IGNORECASE)
    current_text = take_state_screenshot(serial, mode, "startup-current", variant_dir / "startup-current.png")
    gameplay_ready = bool(gameplay_pattern.search(current_text))
    if not gameplay_ready:
        wait_for_main_menu(serial, mode, variant_dir, timeout_s=90)
        # The central Play button is used; bottom navigation icons are never tapped.
        shell(serial, "input tap 960 540")
        wait_for_state(
            serial, mode, "level-select", variant_dir / "level-select.png",
            lambda t: ("stereo madness" in t or "normal mode" in t or "practice mode" in t)
                      and "notice" not in t,
            timeout_s=60
        )
        shell(serial, "input tap 960 500")
        wait_for_state(
            serial, mode, "gameplay-start", variant_dir / "gameplay-start.png",
            is_gameplay_text,
            timeout_s=75
        )
    else:
        screenshot(serial, f"dashcore-{mode.lower()}-gameplay-start-confirmed.png",
                   variant_dir / "gameplay-start.png")

    gameplay_ocr = screenshot_text(variant_dir / "gameplay-start.png")
    gameplay_verified = is_gameplay_text(gameplay_ocr)
    if not gameplay_verified:
        raise RuntimeError(f"{mode}: gameplay HUD percentage was not verified; benchmark aborted.")

    # Wait for 600 complete gameplay updates, then a separate bounded 120-frame
    # culling telemetry sample before resetting frame counters for FPS timing.
    if mod_enabled and culling_mode == "on":
        telemetry_deadline = time.monotonic() + 75
        telemetry_seen = False
        while time.monotonic() < telemetry_deadline:
            found = shell(
                serial,
                f"grep -R -I -l 'CULL-STATS frames=' {shlex.quote(log_dir)} 2>/dev/null",
                check=False, timeout=15
            ).strip()
            if found:
                telemetry_seen = True
                break
            time.sleep(1)
        if not telemetry_seen:
            raise RuntimeError(
                f"{mode}: culling did not emit CULL-STATS after its 600-frame warmup and 120-frame sample; refusing to report an unverified ON run."
            )
        time.sleep(1)

    pids = shell(serial, "pidof com.geode.launcher", check=False).strip().split()
    pid = pids[0] if pids else ""
    gfx = {"available": False, "reason": "Launcher process not found"}
    observation = {"available": False, "reason": "Launcher process not found"}
    surface_layers: list[str] = []
    surface_metrics: dict[str, Any] = {"available": False, "reason": "Launcher process not found"}
    if pid:
        shell(serial, "dumpsys gfxinfo com.geode.launcher reset", check=False)
        surface_layers = prepare_surfaceflinger(serial, variant_dir)
        observation = run_device_observation(serial, pid, variant_dir)
        surface_metrics = collect_surfaceflinger(serial, surface_layers, variant_dir)
        gfx_text = shell(serial, "dumpsys gfxinfo com.geode.launcher framestats", check=False, timeout=90)
        (variant_dir / "gfxinfo-framestats.txt").write_text(gfx_text)
        gfx = parse_gfxinfo(variant_dir / "gfxinfo-framestats.txt")
        if surface_metrics.get("available"):
            gfx = surface_metrics
        elif not gfx.get("available"):
            gfx = surface_metrics

    last_logcat = shell(serial, "logcat -d -v threadtime", check=False)
    (variant_dir / "logcat-final.txt").write_text(last_logcat)
    crash_text = shell(serial, "logcat -b crash -d -v threadtime", check=False)
    (variant_dir / "crash-buffer.txt").write_text(crash_text)
    geode_text = read_remote_logs(serial, log_dir, variant_dir / "geode-logs-final")
    combined = last_logcat + "\n" + geode_text
    culling_runtime = parse_culling_runtime(combined) if mod_enabled else {
        "activation_report_found": False, "requested": False, "active": False,
        "active_confirmed": False, "stats_sample_count": 0, "frames": 0,
        "visited_game_objects": 0, "eligible_objects": 0, "culled_objects": 0,
        "actual_culling_observed": False, "reason_report_found": False,
        "reason_counts": {}, "protected_objects_total": 0,
        "eligible_reason_total": 0, "reason_culled_total": 0,
        "reason_totals_match": False, "nearest_guard_band_candidate": None,
    }
    (variant_dir / "combined-geode-log.txt").write_text(combined)
    screenshot(serial, f"dashcore-{mode.lower()}-final.png", variant_dir / "final-screen.png")
    crash = bool(re.search(r"FATAL EXCEPTION|Fatal signal|SIGABRT|SIGSEGV|has died", crash_text, re.IGNORECASE))
    data = {"mode": mode, "mod_enabled": mod_enabled,
            "culling_mode": culling_mode if mod_enabled else "not-applicable",
            "culling_runtime": culling_runtime,
            "device_profile_id": args.device_profile_data["profile_id"],
            "execution_environment": "cloud_android_emulator", "hardware_equivalence": "NOT_EMULATED",
            "device_profile_assessment": args.device_profile_assessment,
            "gfxinfo": gfx,
            "surfaceflinger": surface_metrics, "surfaceflinger_candidate_count": len(surface_layers),
            "cpu_observation": observation, "crash_detected": crash,
            "gameplay_verified": gameplay_verified, "gameplay_ocr": gameplay_ocr[:1000],
            "launcher_pid_after_test": pid or None}
    (variant_dir / "metrics.json").write_text(json.dumps(data, indent=2) + "\n")
    shell(serial, "am force-stop com.geode.launcher", check=False)
    return data

def write_report(args: argparse.Namespace, baseline: dict[str, Any], enabled: dict[str, Any]) -> bool:
    out = pathlib.Path(args.report_dir); out.mkdir(parents=True, exist_ok=True)
    base_gfx, mod_gfx = baseline["gfxinfo"], enabled["gfxinfo"]
    base_cpu = baseline["cpu_observation"].get("cpu_percent_one_core")
    mod_cpu = enabled["cpu_observation"].get("cpu_percent_one_core")
    def fmt(value: Any) -> str: return "unavailable" if value is None else f"{value:.2f}"
    def fps(item: dict[str, Any], key: str) -> Any:
        return item.get(key) if item.get("available") else None
    base_max, mod_max = fps(base_gfx, "max_fps"), fps(mod_gfx, "max_fps")
    base_avg, mod_avg = fps(base_gfx, "average_fps"), fps(mod_gfx, "average_fps")
    base_min, mod_min = fps(base_gfx, "min_fps"), fps(mod_gfx, "min_fps")
    max_delta = (mod_max-base_max) if base_max is not None and mod_max is not None else None
    avg_delta = (mod_avg-base_avg) if base_avg is not None and mod_avg is not None else None
    valid = all(item.get("gameplay_verified") is True
                and item["gfxinfo"].get("available")
                and item["cpu_observation"].get("available")
                and not item["crash_detected"] for item in (baseline, enabled))
    profile_report = args.device_profile_assessment
    target = profile_report["target"]
    observed = profile_report["observed_cloud_guest"]
    comparison = profile_report["comparison"]
    cpu_reference = profile_report["cpu_calibration_reference"]
    cpu_medians = cpu_reference["median_iterations_per_second"]
    cpu_inventory = cpu_reference["core_inventory"]
    a73_ids = ", ".join(map(str, cpu_inventory.get("Cortex-A73", {}).get("cpu_ids", []))) or "unreported"
    a53_ids = ", ".join(map(str, cpu_inventory.get("Cortex-A53", {}).get("cpu_ids", []))) or "unreported"
    target_size = "×".join(map(str, target["display_px"]))
    observed_size = "×".join(map(str, observed["wm_size_reported_px"])) if observed.get("wm_size_reported_px") else "unavailable"
    lines = [
        "# DashCore AutoTest — FPS baseline vs mod", "",
        "## Device profile", "",
        f'- Reference profile: **{profile_report["display_name"]}** (ID: {profile_report["profile_id"]})',
        "- Execution target: **cloud Android emulator**, not a physical Huawei Y9s.",
        "- Hardware equivalence: **NOT EMULATED**.",
        f"- Reference SoC/GPU: {target['soc']} / {target['gpu']}.",
        f"- Actual cloud guest: {observed.get('manufacturer') or 'unknown'} {observed.get('model') or 'unknown'}; Android {observed.get('android_release') or 'unknown'} (API {observed.get('android_api_level') or 'unknown'}); ABI {observed.get('primary_abi') or 'unknown'}.",
        f"- Guest CPU: {observed.get('cpu_model_reported') or observed.get('hardware') or 'unknown'} ({observed.get('cpu_logical_cores') or 'unknown'} logical cores); guest RAM: {observed.get('memory_total_mb') or 'unknown'} MB.",
        f"- Guest graphics: {observed.get('surfaceflinger_renderer') or observed.get('egl_implementation') or 'renderer unavailable'}; native bridge: {observed.get('native_bridge') or 'unreported'}.",
        f"- Reference display: {target_size}px; guest wm size: {observed_size}px; refresh-rate observation: {observed.get('display_refresh_rate_hz') or 'unavailable'} Hz.",
        f"- Profile comparisons: API={comparison['android_api']}; ARM64 ABI={comparison['arm64_abi']}; resolution={comparison['display_resolution']}.",
        "- The reference profile supplies device identity and a separately labeled physical CPU reference; no emulator CPU/GPU, resolution, density or memory overrides are applied.",
        "",
        "## Physical CPU reference (context only)",
        f"- Physical sample: **{cpu_reference['selected_sample']['sample_id']}**; {cpu_reference['selected_sample']['duration_ms']} ms × {cpu_reference['selected_sample']['repetitions']} repetitions.",
        f"- Cortex-A73 high-capacity candidate (CPU IDs {a73_ids}): **{cpu_medians['higher_capacity_single_core'] / 1_000_000:.2f} M iterations/s** median.",
        f"- Cortex-A53 lower-capacity candidate (CPU IDs {a53_ids}): **{cpu_medians['lower_capacity_single_core'] / 1_000_000:.2f} M iterations/s** median.",
        f"- All allowed cores concurrent: **{cpu_medians['all_allowed_cores_concurrent'] / 1_000_000:.2f} M iterations/s** median.",
        "- This is the measured physical Huawei Y9s reference, not a cloud CPU result, FPS score, or scaling factor. No CPU quota, clock, affinity, or FPS normalization is derived from it.",
        "",
        *[f"- Limitation: {item}" for item in profile_report["limitations"]],
        "",
        f"**Measurement status: {'VALID' if valid else 'INCOMPLETE'}**", "",
        "| Metric | Without DashCore | With DashCore | Difference (mod - baseline) |",
        "|---|---:|---:|---:|",
        f"| **Maximum FPS** | {fmt(base_max)} | {fmt(mod_max)} | {fmt(max_delta)} |",
        f"| Average FPS | {fmt(base_avg)} | {fmt(mod_avg)} | {fmt(avg_delta)} |",
        f"| Minimum FPS | {fmt(base_min)} | {fmt(mod_min)} | {fmt((mod_min-base_min) if mod_min is not None and base_min is not None else None)} |",
        f"| 95th percentile frame time (ms) | {fmt(fps(base_gfx, 'p95_frame_ms'))} | {fmt(fps(mod_gfx, 'p95_frame_ms'))} | — |",
        f"| Frames slower than 50 ms | {base_gfx.get('over_50ms', 'unavailable')} | {mod_gfx.get('over_50ms', 'unavailable')} | — |",
        f"| Frames slower than 100 ms | {base_gfx.get('over_100ms', 'unavailable')} | {mod_gfx.get('over_100ms', 'unavailable')} | — |",
        f"| Sampled frame intervals | {base_gfx.get('interval_count', 'unavailable')} | {mod_gfx.get('interval_count', 'unavailable')} | — |",
        f"| Process CPU usage (% of one core) | {fmt(base_cpu)} | {fmt(mod_cpu)} | — |",
        "", "## Validation", ""
    ]
    for item in (baseline, enabled):
        lines += [f"### {item['mode']}",
                  f"- DashCore loaded: {item['mod_enabled']}",
                  f"- Gameplay positively verified: {item.get('gameplay_verified', False)}",
                  f"- Frame timing data available: {item['gfxinfo'].get('available')}",
                  f"- Process CPU sample available: {item['cpu_observation'].get('available')}",
                  f"- Crash signature detected: {item['crash_detected']}"]
        if not item["gfxinfo"].get("available"):
            lines.append(f"- FPS data reason: {item['gfxinfo'].get('reason', 'unknown')}")
        lines.append("")
    lines += [
        "## Method", "",
        "- First pass launches Geometry Dash with the DashCore .geode file absent from the Geode mods directory.",
        "- Second pass installs the exact same built .geode artifact and restarts the launcher.",
        "- Both passes use the same cloud device, game APK, menu navigation, default first level, scripted touch pattern, and 20-second device-timed observation.",
        "- FPS prefers SurfaceFlinger actualPresentTime timestamps from Geometry Dash-owned layers, which capture native OpenGL presentation; Android gfxinfo is retained only as a fallback diagnostic.",
        "- Maximum FPS is the inverse of the shortest observed presentation interval; average and minimum FPS are derived from the mean and longest interval. At least 10 real frame intervals are required.",
        "- Frame-time outliers are reported separately so a brief FPS peak cannot hide stuttering.",
        "- CPU utilization is process CPU time divided by device-measured elapsed time, expressed relative to one CPU core.",
        "- Cloud guest CPU/ABI/GPU information is reported in device-profile-assessment.json; this is not a physical Huawei Y9s or Mali-G51 measurement.",
        "- Geometry Dash cloud APK version is recorded separately; cloud results are comparative signals, not a guarantee of physical-device performance.", "",
        "A result is VALID only when both passes positively verify the in-level ATTEMPT HUD (not the level-select 0% labels), provide at least 10 plausible presentation intervals and CPU samples, and neither pass reports a crash. INT64_MAX sentinel timestamps and intervals outside 0.1–1000 ms are rejected. Raw layer listings and latency dumps are included for diagnosis."
    ]
    (out / "fps-comparison.md").write_text("\n".join(lines) + "\n")
    (out / "fps-comparison.json").write_text(json.dumps({
        "status": "VALID" if valid else "INCOMPLETE",
        "device_profile": profile_report,
        "cpu_calibration_reference": cpu_reference,
        "maximum_fps_difference": max_delta, "average_fps_difference": avg_delta,
        "without_dashcore": baseline, "with_dashcore": enabled
    }, indent=2) + "\n")
    print("FPS-RESULT: WITHOUT-DASHCORE max={} avg={} min={}".format(fmt(base_max), fmt(base_avg), fmt(base_min)))
    print("FPS-RESULT: WITH-DASHCORE max={} avg={} min={}".format(fmt(mod_max), fmt(mod_avg), fmt(mod_min)))
    print("FPS-RESULT: MAX-DIFFERENCE={} AVG-DIFFERENCE={}".format(fmt(max_delta), fmt(avg_delta)))
    return valid

def write_culling_report(args: argparse.Namespace, disabled: dict[str, Any],
                         enabled: dict[str, Any]) -> tuple[bool, bool]:
    """Compare the same DashCore package with static culling OFF and ON."""
    out = pathlib.Path(args.report_dir)
    out.mkdir(parents=True, exist_ok=True)
    off_gfx, on_gfx = disabled["gfxinfo"], enabled["gfxinfo"]

    def metric(item: dict[str, Any], key: str) -> Any:
        return item.get(key) if item.get("available") else None

    def fmt(value: Any) -> str:
        return "unavailable" if value is None else f"{value:.2f}"

    off_runtime = disabled["culling_runtime"]
    on_runtime = enabled["culling_runtime"]
    measurement_valid = all(
        item.get("gameplay_verified") is True
        and item["gfxinfo"].get("available")
        and item["cpu_observation"].get("available")
        and not item["crash_detected"]
        for item in (disabled, enabled)
    )
    off_mode_confirmed = (
        off_runtime.get("activation_report_found")
        and off_runtime.get("requested") is False
        and off_runtime.get("active") is False
    )
    activation_valid = (
        disabled.get("culling_mode") == "off"
        and off_mode_confirmed
        and enabled.get("culling_mode") == "on"
        and on_runtime.get("activation_report_found")
        and on_runtime.get("requested") is True
        and on_runtime.get("active_confirmed") is True
        and on_runtime.get("stats_sample_count", 0) > 0
        and on_runtime.get("warmup_frames", 0) >= 600
        and on_runtime.get("frames", 0) >= 120
        and on_runtime.get("visited_game_objects", 0) > 0
        and on_runtime.get("eligible_objects", 0) > 0
        and on_runtime.get("reason_report_found") is True
        and on_runtime.get("reason_totals_match") is True
    )
    actually_culled = activation_valid and on_runtime.get("culled_objects", 0) > 0
    status = (
        "INCOMPLETE" if not measurement_valid or not activation_valid
        else "VALID_WITH_SKIPS" if actually_culled
        else "INCONCLUSIVE_NO_OBJECTS_SKIPPED"
    )
    report = {
        "status": status,
        "measurement_valid": measurement_valid,
        "culling_off_confirmed": off_mode_confirmed,
        "culling_activation_valid": activation_valid,
        "actual_culling_observed": actually_culled,
        "culling_off": disabled,
        "culling_on": enabled,
        "cloud_only": True,
        "physical_huawei_y9s_validation": "NOT_RUN",
    }
    (out / "culling-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    rows = [
        "# DashCore culling OFF vs ON", "",
        f"**Status: {status}**", "",
        "- Same DashCore package, same Android cloud emulator, same level-entry path.",
        "- Culling OFF/ON is selected before each full launcher restart through the mod config override.",
        "- This cloud emulator is not a Huawei Y9s / Kirin 710F / Mali-G51.",
        f"- Culling hook active and telemetry verified: **{activation_valid}**",
        f"- Actual skipped GameObject visits observed: **{on_runtime.get('culled_objects', 0)}**",
        "",
        "| Metric | DashCore culling OFF | DashCore culling ON |",
        "|---|---:|---:|",
        f"| Average FPS | {fmt(metric(off_gfx, 'average_fps'))} | {fmt(metric(on_gfx, 'average_fps'))} |",
        f"| Maximum FPS | {fmt(metric(off_gfx, 'max_fps'))} | {fmt(metric(on_gfx, 'max_fps'))} |",
        f"| Minimum FPS | {fmt(metric(off_gfx, 'min_fps'))} | {fmt(metric(on_gfx, 'min_fps'))} |",
        f"| P95 frame time (ms) | {fmt(metric(off_gfx, 'p95_frame_ms'))} | {fmt(metric(on_gfx, 'p95_frame_ms'))} |",
        f"| Frames slower than 50 ms | {metric(off_gfx, 'over_50ms') if off_gfx.get('available') else 'unavailable'} | {metric(on_gfx, 'over_50ms') if on_gfx.get('available') else 'unavailable'} |",
        f"| Frames slower than 100 ms | {metric(off_gfx, 'over_100ms') if off_gfx.get('available') else 'unavailable'} | {metric(on_gfx, 'over_100ms') if on_gfx.get('available') else 'unavailable'} |",
        f"| CPU (% of one core) | {fmt(disabled['cpu_observation'].get('cpu_percent_one_core'))} | {fmt(enabled['cpu_observation'].get('cpu_percent_one_core'))} |",
        "",
        "## Runtime proof", "",
        f"- Culling OFF requested / active: {off_runtime.get('requested')} / {off_runtime.get('active')}",
        f"- Culling OFF state confirmed: {off_mode_confirmed}",
        f"- Culling ON requested / active: {on_runtime.get('requested')} / {on_runtime.get('active')}",
        f"- Warmup frames completed: {on_runtime.get('warmup_frames')}",
        f"- Telemetry sample frames: {on_runtime.get('frames')}",
        f"- Visited GameObjects: {on_runtime.get('visited_game_objects')}",
        f"- Eligible static GameObjects: {on_runtime.get('eligible_objects')}",
        f"- Culled GameObjects: {on_runtime.get('culled_objects')}",
        f"- Per-reason diagnostic present / internally consistent: {on_runtime.get('reason_report_found')} / {on_runtime.get('reason_totals_match')}",
        f"- Protected objects (effect/trigger/grouped/rotating): {on_runtime.get('protected_objects_total')}",
        f"- Eligible objects classified by bounds reason: {on_runtime.get('eligible_reason_total')}",
        f"- Culled-by-axis count (X/Y/both): {on_runtime.get('reason_culled_total')}",
        f"- Bounds rejection reasons: {json.dumps(on_runtime.get('reason_counts', {}), sort_keys=True)}",
        f"- Nearest guard-band candidate: {json.dumps(on_runtime.get('nearest_guard_band_candidate'), sort_keys=True)}",
        "",
        "The per-reason report must classify every visited GameObject exactly once and reconcile eligible/culled totals with CULL-STATS. Missing or inconsistent reason telemetry makes the culling validation INCOMPLETE, not inconclusive.",
        "",
        "VALID_WITH_SKIPS requires the culling hook to be requested and active, at least 600 completed warmup frames, a separate 120-frame telemetry window, and at least one skipped GameObject. If the hook is active but the level gives no safe skip candidate, the result is INCONCLUSIVE rather than a claimed optimization.",
        "",
        "A cloud result is only a functional signal. Visual safety and performance still require physical Huawei Y9s testing; the Geode culling option remains OFF by default.",
    ]
    (out / "culling-comparison.md").write_text("\n".join(rows) + "\n")
    print((out / "culling-comparison.md").read_text())
    return measurement_valid and activation_valid, actually_culled


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--base-dir", default="/sdcard/Android/media/com.geode.launcher")
    parser.add_argument("--mod-id", default="hesta.optgod")
    parser.add_argument("--mod-file", required=True)
    parser.add_argument("--report-dir", default="reports/autotest-fps")
    parser.add_argument("--repetitions", type=int, default=3,
                        help="Number of paired baseline/mod measurements (1-5; default: 3).")
    parser.add_argument("--device-profile", default="profiles/huawei-y9s.json",
                        help="Reference device profile JSON; descriptive only, never applied as emulator overrides.")
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 5:
        parser.error("--repetitions must be between 1 and 5")
    profile_path = pathlib.Path(args.device_profile)
    args.device_profile_data = load_device_profile(profile_path)
    args.device_profile_assessment = assess_device_profile(
        args.device_profile_data, collect_device_fingerprint(args.serial)
    )
    report_root = pathlib.Path(args.report_dir)
    report_root.mkdir(parents=True, exist_ok=True)
    (report_root / "device-profile.json").write_text(
        json.dumps(args.device_profile_data, indent=2) + "\n", encoding="utf-8"
    )
    (report_root / "cloud-device-fingerprint.json").write_text(
        json.dumps(args.device_profile_assessment["observed_cloud_guest"], indent=2) + "\n", encoding="utf-8"
    )
    (report_root / "device-profile-assessment.json").write_text(
        json.dumps(args.device_profile_assessment, indent=2) + "\n", encoding="utf-8"
    )
    print("DEVICE-PROFILE: {} | execution=cloud-emulator | hardware-equivalence=NOT-EMULATED".format(
        args.device_profile_data["profile_id"]
    ))
    shell(args.serial, f"mkdir -p {shlex.quote(args.base_dir + '/game/geode/mods')}")
    original_report_dir = args.report_dir
    paired_results = []
    for repetition in range(1, args.repetitions + 1):
        args.report_dir = str(report_root / f"repeat-{repetition:02d}")
        pathlib.Path(args.report_dir).mkdir(parents=True, exist_ok=True)
        print(f"BENCHMARK-REPETITION: {repetition}/{args.repetitions}")
        # Three-way comparison. Alternate order to reduce warm-up bias while
        # keeping the same artifact, emulator, and level-entry procedure.
        if repetition % 2:
            baseline = run_variant(args, "WITHOUT_DASHCORE", mod_enabled=False)
            enabled = run_variant(args, "WITH_DASHCORE_CULLING_OFF", mod_enabled=True, culling_mode="off")
            culling_enabled = run_variant(args, "WITH_DASHCORE_CULLING_ON", mod_enabled=True, culling_mode="on")
        else:
            culling_enabled = run_variant(args, "WITH_DASHCORE_CULLING_ON", mod_enabled=True, culling_mode="on")
            enabled = run_variant(args, "WITH_DASHCORE_CULLING_OFF", mod_enabled=True, culling_mode="off")
            baseline = run_variant(args, "WITHOUT_DASHCORE", mod_enabled=False)
        pair_valid = write_report(args, baseline, enabled)
        culling_valid, actually_culled = write_culling_report(args, enabled, culling_enabled)
        paired_results.append({
            "repetition": repetition,
            "status": "VALID" if pair_valid else "INCOMPLETE",
            "culling_validation": "VALID_WITH_SKIPS" if culling_valid and actually_culled else
                                  "INCONCLUSIVE_NO_OBJECTS_SKIPPED" if culling_valid else "INCOMPLETE",
            "culling_activation_valid": culling_valid,
            "actual_culling_observed": actually_culled,
            "without_dashcore": baseline,
            "with_dashcore": enabled,
            "with_culling": culling_enabled,
        })
        print((pathlib.Path(args.report_dir) / "fps-comparison.md").read_text())
    args.report_dir = original_report_dir

    def median_metric(mode: str, key: str) -> float | None:
        values = []
        for pair in paired_results:
            value = pair[mode]["gfxinfo"].get(key)
            if pair["status"] == "VALID" and isinstance(value, (int, float)):
                values.append(float(value))
        return statistics.median(values) if len(values) == args.repetitions else None

    baseline_avg = median_metric("without_dashcore", "average_fps")
    enabled_avg = median_metric("with_dashcore", "average_fps")
    culling_avg_values = [
        float(pair["with_culling"]["gfxinfo"]["average_fps"])
        for pair in paired_results
        if pair["culling_activation_valid"] and pair["with_culling"]["gfxinfo"].get("available")
        and isinstance(pair["with_culling"]["gfxinfo"].get("average_fps"), (int, float))
    ]
    culling_avg = statistics.median(culling_avg_values) if len(culling_avg_values) == args.repetitions else None
    culling_activation_count = sum(pair["culling_activation_valid"] for pair in paired_results)
    culling_observed_count = sum(pair["actual_culling_observed"] for pair in paired_results)
    culling_skipped_total = sum(
        pair["with_culling"]["culling_runtime"].get("culled_objects", 0)
        for pair in paired_results
    )
    reason_fields = (
        "protected_effect", "protected_trigger", "protected_grouped", "protected_rotating",
        "missing_director", "content_size_fallbacks", "invalid_bounding_rect", "invalid_anchor", "invalid_world_bounds",
        "invalid_viewport", "within_guard_band", "offscreen_x", "offscreen_y", "offscreen_both",
    )
    culling_reason_totals = {
        key: sum(
            pair["with_culling"]["culling_runtime"].get("reason_counts", {}).get(key, 0)
            for pair in paired_results
        )
        for key in reason_fields
    }
    reason_diagnostics_complete = all(
        pair["with_culling"]["culling_runtime"].get("reason_report_found") is True
        and pair["with_culling"]["culling_runtime"].get("reason_totals_match") is True
        for pair in paired_results
    )
    summary_valid = all(
        pair["status"] == "VALID" and pair["culling_activation_valid"]
        for pair in paired_results
    )
    summary = {
        "status": "VALID" if summary_valid else "INCOMPLETE",
        "repetitions_requested": args.repetitions,
        "repetitions_valid": sum(pair["status"] == "VALID" for pair in paired_results),
        "aggregation": "median across valid paired runs; no emulator-to-phone equivalence claimed",
        "median_without_dashcore_average_fps": baseline_avg,
        "median_with_dashcore_average_fps": enabled_avg,
        "median_with_culling_average_fps": culling_avg,
        "median_culling_average_fps_change_vs_culling_off": (
            (culling_avg / enabled_avg - 1.0) * 100.0
            if enabled_avg and culling_avg is not None else None
        ),
        "culling_activation_valid_repetitions": culling_activation_count,
        "culling_repetitions_with_actual_skips": culling_observed_count,
        "culling_total_objects_skipped_in_final_telemetry_samples": culling_skipped_total,
        "culling_reason_diagnostics_complete": reason_diagnostics_complete,
        "culling_reason_totals_across_repetitions": culling_reason_totals,
        "culling_validation_status": (
            "VALID_WITH_SKIPS" if culling_activation_count == args.repetitions and culling_observed_count == args.repetitions
            else "INCONCLUSIVE_NO_OBJECTS_SKIPPED" if culling_activation_count == args.repetitions
            else "INCOMPLETE"
        ),
        "median_average_fps_change_percent": (
            (enabled_avg / baseline_avg - 1.0) * 100.0
            if baseline_avg and enabled_avg is not None else None
        ),
        "device_profile": args.device_profile_assessment,
        "cpu_calibration_reference": args.device_profile_assessment["cpu_calibration_reference"],
        "repetitions": paired_results,
        "physical_huawei_y9s_validation": "CPU_REFERENCE_CAPTURED_FPS_NOT_RUN",
        "physical_huawei_y9s_cpu_reference_validation": "CAPTURED",
        "physical_huawei_y9s_fps_validation": "NOT_RUN_CLOUD_ONLY",
        "physical_fps_validation_required_before_non_alpha_release": True,
    }
    (report_root / "fps-repeated-summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    lines = [
        "# DashCore repeated FPS benchmark", "",
        f"- Status: **{summary['status']}**",
        f"- Valid paired repetitions: **{summary['repetitions_valid']}/{args.repetitions}**",
        "- Environment: cloud Android emulator; not the physical Huawei Y9s.",
        "- Hardware equivalence: **NOT EMULATED**.",
        "- Aggregation: median of each metric across all valid paired repetitions.",
        f"- Median average FPS without DashCore: **{baseline_avg if baseline_avg is not None else 'unavailable'}**",
        f"- Median average FPS with DashCore (culling OFF): **{enabled_avg if enabled_avg is not None else 'unavailable'}**",
        f"- Median average FPS with culling ON: **{culling_avg if culling_avg is not None else 'unavailable'}**",
        f"- Culling ON average-FPS change vs culling OFF: **{summary['median_culling_average_fps_change_vs_culling_off'] if summary['median_culling_average_fps_change_vs_culling_off'] is not None else 'unavailable'}%**",
        f"- Culling runtime validation: **{summary['culling_validation_status']}**",
        f"- Repetitions with culling hook active: **{culling_activation_count}/{args.repetitions}**",
        f"- Repetitions with actual objects skipped: **{culling_observed_count}/{args.repetitions}**",
        f"- Objects skipped in final telemetry samples (summed): **{culling_skipped_total}**",
        f"- Per-reason diagnostics complete and reconciled: **{reason_diagnostics_complete}**",
        f"- Per-reason totals across repetitions: **{json.dumps(culling_reason_totals, sort_keys=True)}**",
        "- Physical Huawei Y9s FPS validation: **NOT RUN (this workflow is cloud-only)**.",
        f"- Physical CPU reference sample: **{summary['cpu_calibration_reference']['selected_sample']['sample_id']}**; medians are included for context only.", "",
        "| Repetition | Status | No mod avg FPS | Culling OFF avg FPS | Culling ON avg FPS | Culling validation |",
        "|---:|---|---:|---:|---:|---|",
    ]
    for pair in paired_results:
        b = pair["without_dashcore"]["gfxinfo"].get("average_fps")
        m = pair["with_dashcore"]["gfxinfo"].get("average_fps")
        c = pair["with_culling"]["gfxinfo"].get("average_fps")
        lines.append(f"| {pair['repetition']} | {pair['status']} | {b if b is not None else 'unavailable'} | {m if m is not None else 'unavailable'} | {c if c is not None else 'unavailable'} | {pair['culling_validation']} |")
    lines += ["", "Cloud measurements are comparative signals only. Do not publish a non-alpha optimization release until validated on the physical reference phone."]
    (report_root / "fps-repeated-summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print((report_root / "fps-repeated-summary.md").read_text())
    if not summary_valid:
        print("::error::Repeated benchmark incomplete; baseline A/B or culling activation/telemetry validation failed.")
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
