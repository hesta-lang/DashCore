#!/usr/bin/env python3
"""Run a paired DashCore fast-path OFF/ON benchmark on one Android cloud device."""
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
    if not path.exists(): return {"available": False}
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
    if not intervals: return {"available": False, "reason": "No FrameCompleted timestamps parsed"}
    return {"available": True, "interval_count": len(intervals), "mean_ms": statistics.mean(intervals),
            "p95_ms": percentile(intervals, .95), "max_ms": max(intervals),
            "over_50ms": sum(value > 50.0 for value in intervals)}

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

def summarize_logs(text: str, mode: str) -> dict[str, Any]:
    return {
        "mode_confirmed": f"FAST-PATH: mode={mode}" in text,
        "profile_complete": "both 600-frame samples complete" in text and "NODE-VISIT-COST FINAL" in text,
        "update_profile": parse_update(text),
        "node_visit_final_present": "NODE-VISIT-COST FINAL" in text,
        "culling_active_false": "culling-active=false" in text,
    }

def notice_is_visible(path: pathlib.Path) -> bool:
    image = Image.open(path).convert("RGB")
    width, height = image.size
    # The initial consent dialog has a bright green OK button centered at ~50% x, 73% y.
    x0, x1 = int(width * 0.44), int(width * 0.56)
    y0, y1 = int(height * 0.69), int(height * 0.79)
    green = 0
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            r, g, b = image.getpixel((x, y))
            if g > 150 and r < 155 and b < 120:
                green += 1
    return green > 80

def run_variant(args: argparse.Namespace, mode: str, first_run: bool) -> dict[str, Any]:
    serial, base = args.serial, args.base_dir.rstrip("/")
    config_dir = f"{base}/game/geode/config/{args.mod_id}"
    log_dir = f"{base}/game/geode/logs"
    variant_dir = pathlib.Path(args.report_dir) / mode.lower()
    variant_dir.mkdir(parents=True, exist_ok=True)
    shell(serial, "am force-stop com.geode.launcher", check=False); time.sleep(2)
    shell(serial, f"mkdir -p {shlex.quote(config_dir)}")
    shell(serial, f"printf '%s\\n' {shlex.quote(mode.lower())} > {shlex.quote(config_dir + '/fast-path-mode.txt')}")
    shell(serial, f"mkdir -p {shlex.quote(log_dir)} && rm -f {shlex.quote(log_dir)}/*")
    shell(serial, "logcat -c", check=False)
    shell(serial, "am start -a android.intent.action.VIEW -d geode-launcher://main/launch -p com.geode.launcher", check=False)
    time.sleep(9)
    # Consent dialog is shown on each fresh launcher process in this cloud image.
    notice_path = variant_dir / "notice-check.png"
    for attempt in range(4):
        screenshot(serial, f"dashcore-ab-{mode.lower()}-notice-{attempt}.png", notice_path)
        if not notice_is_visible(notice_path):
            break
        shell(serial, "input tap 960 792", check=False)
        time.sleep(1.5)
    screenshot(serial, f"dashcore-ab-{mode.lower()}-main.png", variant_dir / "main-menu.png")
    if notice_is_visible(variant_dir / "main-menu.png"):
        raise RuntimeError(f"{mode}: Geometry Dash consent dialog remained visible after four taps; refusing to record an invalid gameplay pass.")
    # The main-menu Play button is the center button in the bottom row.
    shell(serial, "input tap 960 925"); time.sleep(7)
    screenshot(serial, f"dashcore-ab-{mode.lower()}-level.png", variant_dir / "level-select.png")
    # Level page Play button is centered in the panel.
    shell(serial, "input tap 960 500"); time.sleep(7)
    screenshot(serial, f"dashcore-ab-{mode.lower()}-gameplay-start.png", variant_dir / "gameplay-start.png")
    profile_complete = False
    for second in range(1, 121):
        shell(serial, "input tap 700 800", check=False); time.sleep(.32)
        shell(serial, "input tap 700 800", check=False); time.sleep(.68)
        if second % 5 == 0:
            screenshot(serial, f"dashcore-ab-{mode.lower()}-gameplay-{second}s.png", variant_dir / f"gameplay-{second}s.png")
            logcat = shell(serial, "logcat -d -v threadtime", check=False)
            (variant_dir / f"logcat-{second}s.txt").write_text(logcat)
            geode_checkpoint = read_remote_logs(serial, log_dir, variant_dir / f"geode-logs-checkpoint-{second}s")
            checkpoint = logcat + "\n" + geode_checkpoint
            if "both 600-frame samples complete" in checkpoint and "NODE-VISIT-COST FINAL" in checkpoint:
                profile_complete = True; break
    time.sleep(2)
    pids = shell(serial, "pidof com.geode.launcher", check=False).strip().split()
    pid = pids[0] if pids else ""
    observation = {"available": False, "reason": "Profile did not complete; fast path was not measured"}
    gfx = {"available": False, "reason": "Profile did not complete"}
    if profile_complete and pid:
        shell(serial, "dumpsys gfxinfo com.geode.launcher reset", check=False)
        observation = run_device_observation(serial, pid, variant_dir)
        gfx_text = shell(serial, "dumpsys gfxinfo com.geode.launcher framestats", check=False, timeout=90)
        (variant_dir / "gfxinfo-framestats.txt").write_text(gfx_text)
        gfx = parse_gfxinfo(variant_dir / "gfxinfo-framestats.txt")
    last_logcat = shell(serial, "logcat -d -v threadtime", check=False)
    (variant_dir / "logcat-final.txt").write_text(last_logcat)
    crash_text = shell(serial, "logcat -b crash -d -v threadtime", check=False)
    (variant_dir / "crash-buffer.txt").write_text(crash_text)
    geode_text = read_remote_logs(serial, log_dir, variant_dir / "geode-logs-final")
    combined = last_logcat + "\n" + geode_text
    (variant_dir / "combined-geode-log.txt").write_text(combined)
    screenshot(serial, f"dashcore-ab-{mode.lower()}-final.png", variant_dir / "final-screen.png")
    log_summary = summarize_logs(combined, mode)
    crash = bool(re.search(r"FATAL EXCEPTION|Fatal signal|SIGABRT|SIGSEGV|has died", crash_text, re.IGNORECASE))
    data = {"mode": mode, "first_run": first_run, **log_summary,
            "profile_complete": bool(profile_complete and log_summary["profile_complete"]),
            "cpu_observation": observation, "gfxinfo": gfx, "crash_detected": crash,
            "launcher_pid_after_test": pid or None}
    (variant_dir / "metrics.json").write_text(json.dumps(data, indent=2) + "\n")
    shell(serial, "am force-stop com.geode.launcher", check=False)
    return data

def write_report(args: argparse.Namespace, off: dict[str, Any], on: dict[str, Any]) -> bool:
    out = pathlib.Path(args.report_dir); out.mkdir(parents=True, exist_ok=True)
    valid = all(item["mode_confirmed"] and item["profile_complete"] and
                item["cpu_observation"].get("available") and not item["crash_detected"] for item in (off, on))
    def fmt(value: Any) -> str: return "unavailable" if value is None else f"{value:.2f}"
    off_cpu, on_cpu = off["cpu_observation"].get("cpu_percent_one_core"), on["cpu_observation"].get("cpu_percent_one_core")
    cpu_change = ((off_cpu-on_cpu)/off_cpu*100.0 if off_cpu is not None and on_cpu is not None and off_cpu > 0 else None)
    off_gfx, on_gfx = off["gfxinfo"], on["gfxinfo"]
    off_p95 = off_gfx.get("p95_ms") if off_gfx.get("available") else None
    on_p95 = on_gfx.get("p95_ms") if on_gfx.get("available") else None
    gfx_change = ((off_p95-on_p95)/off_p95*100.0 if off_p95 is not None and on_p95 is not None and off_p95 > 0 else None)
    lines = [
        "# DashCore AutoTest v4.3 — Fast-Path A/B", "",
        f"**Comparison status: {'VALID' if valid else 'INCOMPLETE'}**", "",
        "| Measurement | Fast path OFF | Fast path ON | ON improvement |",
        "|---|---:|---:|---:|",
        f"| Launcher CPU utilization (one-core %) | {fmt(off_cpu)} | {fmt(on_cpu)} | {fmt(cpu_change)}% |",
        f"| gfxinfo frame interval p95 (ms) | {fmt(off_p95)} | {fmt(on_p95)} | {fmt(gfx_change)}% |",
        f"| gfxinfo frame intervals sampled | {off_gfx.get('interval_count', 'unavailable')} | {on_gfx.get('interval_count', 'unavailable')} | — |",
        "", "## Run validation", ""
    ]
    for item in (off, on):
        lines += [f"### {item['mode']}", f"- Mode confirmed in DashCore log: {item['mode_confirmed']}",
                  f"- Both 600-frame diagnostic windows completed: {item['profile_complete']}",
                  f"- Process CPU sample: {item['cpu_observation'].get('available')}",
                  f"- gfxinfo frame data: {item['gfxinfo'].get('available')}",
                  f"- Crash signature detected: {item['crash_detected']}",
                  f"- Node visit final marker: {item['node_visit_final_present']}",
                  f"- Culling active false marker: {item['culling_active_false']}"]
        update = item.get("update_profile")
        if update:
            lines.append(f"- Diagnostic UPDATE profile (before fast-path observation): {update['frames']} frames, avg {update['avg_us']}us, max {update['max_us']}us, >8ms {update['over8ms']}, gaps >50ms {update['gaps_over_50ms']}, max gap {update['max_gap_us']}us")
        lines.append("")
    lines += [
        "## Method", "",
        "- The same DashCore .geode package and cloud Android instance are used for both passes.",
        "- The app is restarted between passes; the mod reads fast-path-mode.txt once at startup.",
        "- Each pass follows the same menu taps into the default first level and sends the same scripted jump taps.",
        "- Both 600-update and 600-draw diagnostic windows must finish before the separate 20-second process-CPU observation begins. The fast path is evaluated only in this post-profile window.",
        "- Android gfxinfo framestats is best-effort. Missing frame timestamps are marked unavailable and do not stop the A/B run.",
        "- CPU utilization is process CPU time divided by device-measured elapsed time; it is expressed relative to one CPU core.",
        "- UPDATE and NODE-VISIT diagnostics are validation controls, not the fast-path performance result, because the fast path intentionally bypasses those hooks only after the diagnostic window ends.",
        "- This is one cloud emulator (x86_64 with ARM translation), not a physical Huawei STK-LX3 / Mali-G51 measurement. Scripted inputs do not guarantee identical gameplay state or level completion.",
        "- Live object culling remains hard-disabled in both passes.", "",
        "A result is VALID only when the mod confirms both requested modes, both diagnostic windows finish, both process-CPU samples are available, and neither pass reports a crash."
    ]
    (out / "comparison.md").write_text("\n".join(lines) + "\n")
    (out / "comparison.json").write_text(json.dumps({"status": "VALID" if valid else "INCOMPLETE",
        "cpu_improvement_percent": cpu_change, "gfxinfo_p95_improvement_percent": gfx_change,
        "off": off, "on": on}, indent=2) + "\n")
    return valid

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--base-dir", default="/sdcard/Android/media/com.geode.launcher")
    parser.add_argument("--mod-id", default="hesta.optgod")
    parser.add_argument("--mod-file", required=True)
    parser.add_argument("--report-dir", default="reports/autotest-v4.3")
    args = parser.parse_args()
    shell(args.serial, f"mkdir -p {shlex.quote(args.base_dir + '/game/geode/mods')}")
    shell(args.serial, f"mkdir -p {shlex.quote(args.base_dir + '/game/geode/config/' + args.mod_id)}")
    mod_remote = args.base_dir + "/game/geode/mods/DashCore.geode"
    run_adb(args.serial, "push", args.mod_file, mod_remote, timeout=120)
    shell(args.serial, f"test -s {shlex.quote(mod_remote)}")
    off = run_variant(args, "OFF", first_run=True)
    on = run_variant(args, "ON", first_run=False)
    valid = write_report(args, off, on)
    print((pathlib.Path(args.report_dir) / "comparison.md").read_text())
    if not valid:
        print("::error::Fast-path A/B comparison is incomplete; see reports/autotest-v4.2/comparison.md")
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
