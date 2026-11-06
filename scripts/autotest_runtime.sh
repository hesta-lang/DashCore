#!/usr/bin/env bash
set -Eeuo pipefail

REPORT_DIR="${REPORT_DIR:-reports/runtime}"
ARTIFACT_DIR="${ARTIFACT_DIR:-artifacts}"
PACKAGE_NAME="${PACKAGE_NAME:-com.geode.launcher}"
APK_PATH="${GITHUB_WORKSPACE:-.}/test-input/geode-launcher.apk"
MOD_DIR="/sdcard/Android/media/${PACKAGE_NAME}/game/geode/mods"
mkdir -p "$REPORT_DIR"

capture_diagnostics() {
  set +e
  adb logcat -d -v threadtime > "$REPORT_DIR/logcat.txt" 2>&1
  adb logcat -b crash -d -v threadtime > "$REPORT_DIR/crash-buffer.txt" 2>&1
  adb shell dumpsys activity activities > "$REPORT_DIR/activities.txt" 2>&1
  adb shell screencap -p /sdcard/dashcore-autotest.png >/dev/null 2>&1
  adb pull /sdcard/dashcore-autotest.png "$REPORT_DIR/screenshot.png" >/dev/null 2>&1
  adb pull "/sdcard/Android/data/$PACKAGE_NAME/files/geode/logs" "$REPORT_DIR/geode-logs-internal" >/dev/null 2>&1
  adb pull "/sdcard/Android/media/$PACKAGE_NAME/game/geode/logs" "$REPORT_DIR/geode-logs-media" >/dev/null 2>&1
}
trap capture_diagnostics EXIT

echo "Waiting for Android emulator..."
adb wait-for-device
for attempt in $(seq 1 60); do
  boot="$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')"
  [ "$boot" = "1" ] && break
  sleep 2
done
[ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = "1" ] || {
  echo "Android emulator did not finish booting." >&2
  exit 1
}

{
  echo "SDK=$(adb shell getprop ro.build.version.sdk | tr -d '\r')"
  echo "Release=$(adb shell getprop ro.build.version.release | tr -d '\r')"
  echo "ABI=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
  echo "NativeBridge=$(adb shell getprop ro.dalvik.vm.native.bridge | tr -d '\r')"
} | tee "$REPORT_DIR/device.txt"

test -s "$APK_PATH"
AAPT="$(find "${ANDROID_HOME:-/usr/local/lib/android/sdk}/build-tools" -type f -name aapt 2>/dev/null | sort -V | tail -n 1)"
if [ -z "$AAPT" ]; then
  AAPT="$(find "${ANDROID_SDK_ROOT:-/usr/local/lib/android/sdk}/build-tools" -type f -name aapt 2>/dev/null | sort -V | tail -n 1)"
fi
[ -n "$AAPT" ] || { echo "Android aapt tool not found." >&2; exit 1; }

APK_BADGING="$("$AAPT" dump badging "$APK_PATH")"
APK_PACKAGE="$(printf '%s\n' "$APK_BADGING" | sed -n "s/^package: name='\([^']*\)'.*/\1/p" | head -n 1)"
echo "APK package: $APK_PACKAGE" | tee "$REPORT_DIR/apk.txt"
if [ "$APK_PACKAGE" != "$PACKAGE_NAME" ]; then
  echo "Expected package $PACKAGE_NAME, got $APK_PACKAGE." | tee -a "$REPORT_DIR/apk.txt"
  exit 1
fi

GEODE_PACKAGE="$(find "$ARTIFACT_DIR" -type f -name '*.geode' -print -quit)"
[ -n "$GEODE_PACKAGE" ] || { echo "No .geode package found." >&2; exit 1; }
cp "$GEODE_PACKAGE" "$REPORT_DIR/DashCore.geode"

echo "Installing Geode Launcher..."
adb install -r "$APK_PATH" | tee "$REPORT_DIR/install.txt"
adb shell pm path "$PACKAGE_NAME" | tee -a "$REPORT_DIR/install.txt"

echo "Preparing Geode mods directory..."
adb shell mkdir -p "$MOD_DIR"
adb push "$GEODE_PACKAGE" "$MOD_DIR/DashCore.geode" | tee "$REPORT_DIR/mod-push.txt"
adb shell ls -l "$MOD_DIR" | tee -a "$REPORT_DIR/mod-push.txt"

echo "Launching Geometry Dash / Geode Launcher..."
adb shell am force-stop "$PACKAGE_NAME" || true
adb shell monkey -p "$PACKAGE_NAME" -c android.intent.category.LAUNCHER 1 | tee "$REPORT_DIR/launch.txt"
sleep 20
adb shell am start -a android.intent.action.VIEW -d "geode-launcher://main/launch" -p "$PACKAGE_NAME" | tee -a "$REPORT_DIR/launch.txt"
sleep 50

PID="$(adb shell pidof "$PACKAGE_NAME" 2>/dev/null | tr -d '\r' || true)"
if [ -z "$PID" ]; then
  echo "Launcher process is not running after startup wait." | tee "$REPORT_DIR/result.txt"
  exit 1
fi
echo "Launcher process is running: $PID" | tee "$REPORT_DIR/result.txt"

adb logcat -d -v threadtime > "$REPORT_DIR/logcat-after-launch.txt"
adb logcat -b crash -d -v threadtime > "$REPORT_DIR/crash-buffer-after-launch.txt"
if grep -E "$PACKAGE_NAME|pid: $PID" "$REPORT_DIR/crash-buffer-after-launch.txt" | grep -Ei "FATAL EXCEPTION|Fatal signal|SIGABRT|SIGSEGV|has died" > "$REPORT_DIR/app-crashes.txt"; then
  echo "A crash signature associated with the launcher was detected." | tee -a "$REPORT_DIR/result.txt"
  exit 1
else
  echo "No launcher crash signature found in Android crash buffer." | tee -a "$REPORT_DIR/result.txt"
fi

GEODE_LOG_ROOT_INTERNAL="/sdcard/Android/data/$PACKAGE_NAME/files/geode"
GEODE_LOG_ROOT_MEDIA="/sdcard/Android/media/$PACKAGE_NAME/game/geode"
adb shell "find $GEODE_LOG_ROOT_INTERNAL $GEODE_LOG_ROOT_MEDIA -maxdepth 5 -type f 2>/dev/null | head -150" > "$REPORT_DIR/geode-files.txt" || true
adb pull "$GEODE_LOG_ROOT_INTERNAL/logs" "$REPORT_DIR/geode-logs-internal" >/dev/null 2>&1 || true
adb pull "$GEODE_LOG_ROOT_MEDIA/logs" "$REPORT_DIR/geode-logs-media" >/dev/null 2>&1 || true

if grep -R -I -E "hesta\.optgod|DashCore" "$REPORT_DIR"/geode-logs-* > "$REPORT_DIR/mod-log-matches.txt" 2>/dev/null; then
  echo "DashCore identifier was found in Geode logs." | tee -a "$REPORT_DIR/result.txt"
  MOD_LOG_CONFIRMED="true"
else
  echo "WARNING: app launched, but DashCore load was not confirmed in collected Geode logs." | tee -a "$REPORT_DIR/result.txt"
  MOD_LOG_CONFIRMED="false"
fi

python3 - "$REPORT_DIR" "$PID" "$MOD_LOG_CONFIRMED" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
report = {
    "result": "passed",
    "launcher_process": sys.argv[2],
    "dashcore_log_identifier_found": sys.argv[3] == "true",
    "checks": [
        "Android emulator booted",
        "Geode Launcher APK package ID verified",
        "Geode Launcher installed",
        "DashCore .geode copied to Geode mods directory",
        "Launcher started and remained alive for 50 seconds",
        "No launcher crash signature found in crash buffer",
    ],
    "limitations": [
        "This is an Android emulator smoke test, not a physical Huawei/Mali-G51 test.",
        "The emulator uses Android 15 API 35 with software graphics.",
        "DashCore load is only confirmed if its identifier appears in collected Geode logs.",
        "A successful launch does not validate gameplay rendering, trigger safety, frame pacing, or GPU performance.",
    ],
}
if report["dashcore_log_identifier_found"]:
    report["checks"].append("DashCore identifier found in Geode logs")
else:
    report["warnings"] = ["DashCore load could not be confirmed from collected Geode logs."]
(root / "runtime-report.json").write_text(json.dumps(report, indent=2) + "\n")
lines = [
    "# DashCore AutoTest v2 — runtime report",
    "",
    "**Result: PASSED — launcher smoke test**",
    "",
    *[f"- {item}" for item in report["checks"]],
    "",
    "## Warnings",
    *[f"- {item}" for item in report.get("warnings", [])] or ["- None"],
    "",
    "## Limitations",
    *[f"- {item}" for item in report["limitations"]],
]
(root / "summary.md").write_text("\n".join(lines) + "\n")
PY

echo "Runtime smoke test completed."
