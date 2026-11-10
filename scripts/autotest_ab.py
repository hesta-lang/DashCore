#!/usr/bin/env python3
"""Measure Geometry Dash frame timing first without DashCore, then with DashCore."""
from __future__ import annotations
import argparse, csv, json, math, pathlib, re, shlex, statistics, subprocess, sys, time
from PIL import Image
from typing import Any

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
    """Parse SurfaceFlinger --latency output (refresh period + desired/present/ready ns)."""
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
            # SurfaceFlinger reports desiredPresentTime, actualPresentTime, frameReadyTime.
            stamp = int(fields[1])
        except ValueError:
            continue
        # SurfaceFlinger uses INT64_MAX as an invalid/unavailable timestamp.
        if 0 < stamp < (2**63 - 1): present_ns.append(stamp)
    present_ns = sorted(set(present_ns))
    intervals = [(b-a)/1_000_000.0 for a, b in zip(present_ns, present_ns[1:])
                 if b > a and 0.1 <= (b-a)/1_000_000.0 <= 1000.0]
    if len(intervals) < 10:
        return {"available": False, "reason": f"Insufficient SurfaceFlinger frames ({len(intervals)} intervals)",
                "frame_count": len(present_ns), "refresh_period_ns": refresh_ns}
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
        if predicate(last_text):
            screenshot(serial, f"dashcore-{mode.lower()}-{label}-confirmed.png", destination)
            (destination.parent / f"{label}-confirmed-ocr.txt").write_text(last_text + "\n")
            return last_text
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
    sizes = re.findall(r"(?:Override|Physical) size:\\s*(\\d+)x(\\d+)", display_text)
    if sizes:
        display_width, display_height = map(int, sizes[-1])
    else:
        display_width, display_height = width, height
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
        if "play" in last_text and ("geometry dash" in last_text or "settings" in last_text or "editor" in last_text):
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

def run_variant(args: argparse.Namespace, mode: str, mod_enabled: bool) -> dict[str, Any]:
    serial, base = args.serial, args.base_dir.rstrip("/")
    mod_dir = f"{base}/game/geode/mods"
    mod_remote = f"{mod_dir}/DashCore.geode"
    log_dir = f"{base}/game/geode/logs"
    variant_dir = pathlib.Path(args.report_dir) / mode.lower().replace("_", "-")
    variant_dir.mkdir(parents=True, exist_ok=True)

    shell(serial, "am force-stop com.geode.launcher", check=False); time.sleep(2)
    shell(serial, f"mkdir -p {shlex.quote(mod_dir)}")
    shell(serial, f"rm -f {shlex.quote(mod_remote)}")
    if mod_enabled:
        run_adb(serial, "push", args.mod_file, mod_remote, timeout=120)
        shell(serial, f"test -s {shlex.quote(mod_remote)}")
    shell(serial, f"mkdir -p {shlex.quote(log_dir)} && rm -f {shlex.quote(log_dir)}/*")
    shell(serial, "logcat -c", check=False)
    shell(serial, "am start -a android.intent.action.VIEW -d geode-launcher://main/launch -p com.geode.launcher", check=False)

    startup_text = wait_for_state(
        serial, mode, "startup", variant_dir / "startup.png",
        lambda t: bool(t.strip()), timeout_s=75
    )
    if "notice" in startup_text or "terms of use" in startup_text:
        wait_for_notice_dismissal(serial, mode, variant_dir)

    gameplay_pattern = re.compile(r"\b\d{1,3}\s*%")
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
            lambda t: bool(gameplay_pattern.search(t)),
            timeout_s=75
        )
    else:
        screenshot(serial, f"dashcore-{mode.lower()}-gameplay-start-confirmed.png",
                   variant_dir / "gameplay-start.png")

    gameplay_ocr = screenshot_text(variant_dir / "gameplay-start.png")
    gameplay_verified = bool(gameplay_pattern.search(gameplay_ocr))
    if not gameplay_verified:
        raise RuntimeError(f"{mode}: gameplay HUD percentage was not verified; benchmark aborted.")

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
    (variant_dir / "combined-geode-log.txt").write_text(combined)
    screenshot(serial, f"dashcore-{mode.lower()}-final.png", variant_dir / "final-screen.png")
    crash = bool(re.search(r"FATAL EXCEPTION|Fatal signal|SIGABRT|SIGSEGV|has died", crash_text, re.IGNORECASE))
    data = {"mode": mode, "mod_enabled": mod_enabled, "gfxinfo": gfx,
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
    lines = [
        "# DashCore AutoTest — FPS baseline vs mod", "",
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
        "- This cloud emulator is x86_64 with ARM translation; it is not a physical Huawei STK-LX3 / Mali-G51 measurement.",
        "- Geometry Dash cloud APK version is recorded separately; cloud results are comparative signals, not a guarantee of physical-device performance.", "",
        "A result is VALID only when both passes positively verify the in-game percentage HUD, provide at least 10 plausible presentation intervals and CPU samples, and neither pass reports a crash. INT64_MAX sentinel timestamps and intervals outside 0.1–1000 ms are rejected. Raw layer listings and latency dumps are included for diagnosis."
    ]
    (out / "fps-comparison.md").write_text("\n".join(lines) + "\n")
    (out / "fps-comparison.json").write_text(json.dumps({
        "status": "VALID" if valid else "INCOMPLETE",
        "maximum_fps_difference": max_delta, "average_fps_difference": avg_delta,
        "without_dashcore": baseline, "with_dashcore": enabled
    }, indent=2) + "\n")
    print("FPS-RESULT: WITHOUT-DASHCORE max={} avg={} min={}".format(fmt(base_max), fmt(base_avg), fmt(base_min)))
    print("FPS-RESULT: WITH-DASHCORE max={} avg={} min={}".format(fmt(mod_max), fmt(mod_avg), fmt(mod_min)))
    print("FPS-RESULT: MAX-DIFFERENCE={} AVG-DIFFERENCE={}".format(fmt(max_delta), fmt(avg_delta)))
    return valid

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--base-dir", default="/sdcard/Android/media/com.geode.launcher")
    parser.add_argument("--mod-id", default="hesta.optgod")
    parser.add_argument("--mod-file", required=True)
    parser.add_argument("--report-dir", default="reports/autotest-fps")
    args = parser.parse_args()
    shell(args.serial, f"mkdir -p {shlex.quote(args.base_dir + '/game/geode/mods')}")
    baseline = run_variant(args, "WITHOUT_DASHCORE", mod_enabled=False)
    enabled = run_variant(args, "WITH_DASHCORE", mod_enabled=True)
    valid = write_report(args, baseline, enabled)
    print((pathlib.Path(args.report_dir) / "fps-comparison.md").read_text())
    if not valid:
        print("::error::FPS comparison incomplete; Android frame timestamps or CPU samples are missing, or a crash was detected.")
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
