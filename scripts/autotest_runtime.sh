#!/usr/bin/env bash
set -Eeuo pipefail

REPORT_DIR="${REPORT_DIR:-reports/runtime}"
ARTIFACT_DIR="${ARTIFACT_DIR:-artifacts}"
PACKAGE_NAME="${PACKAGE_NAME:-com.geode.launcher}"
APK_PATH="${GITHUB_WORKSPACE:-.}/test-input/geode-launcher.apk"
APP_FILES_DIR="/sdcard/Android/data/${PACKAGE_NAME}/files"
STAGING_DIR="$APP_FILES_DIR/DashCore"
MOD_DIR="$APP_FILES_DIR/game/geode/mods"
mkdir -p "$REPORT_DIR"

# AVD/ADB can briefly report "offline" immediately after boot. Reconnect
# without failing the whole test on the first transient transport error.
adb_ready() {
  local attempt
  for attempt in $(seq 1 45); do
    if adb get-state 2>/dev/null | grep -qx device; then
      local boot
      boot="$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' || true)"
      if [ "$boot" = "1" ]; then return 0; fi
    fi
    adb reconnect offline >/dev/null 2>&1 || true
    sleep 2
  done
  echo "ADB did not become ready after retries." >&2
  return 1
}

adb_retry() {
  local attempt
  for attempt in $(seq 1 8); do
    if adb "$@"; then return 0; fi
    echo "ADB command failed (attempt $attempt/8): adb $*" >&2
    adb reconnect offline >/dev/null 2>&1 || true
    sleep 2
  done
  echo "ADB command failed after 8 attempts: adb $*" >&2
  return 1
}

capture_diagnostics() {
  set +e
  adb logcat -d -v threadtime > "$REPORT_DIR/logcat.txt" 2>&1
  adb logcat -b crash -d -v threadtime > "$REPORT_DIR/crash-buffer.txt" 2>&1
  adb shell dumpsys activity activities > "$REPORT_DIR/activities.txt" 2>&1
  adb shell dumpsys mount > "$REPORT_DIR/mount-service.txt" 2>&1
  adb shell sm list-volumes all > "$REPORT_DIR/storage-volumes.txt" 2>&1
  adb shell df -h > "$REPORT_DIR/storage-df.txt" 2>&1
  adb shell ls -la /sdcard /storage/emulated/0 /sdcard/Android > "$REPORT_DIR/storage-paths.txt" 2>&1
  adb shell screencap -p /sdcard/dashcore-autotest.png >/dev/null 2>&1
  adb pull /sdcard/dashcore-autotest.png "$REPORT_DIR/screenshot.png" >/dev/null 2>&1
  adb pull "/sdcard/Android/data/$PACKAGE_NAME/files/game/geode/logs" "$REPORT_DIR/geode-logs-app-files" >/dev/null 2>&1
  adb pull "/sdcard/Android/media/$PACKAGE_NAME/game/geode/logs" "$REPORT_DIR/geode-logs-media" >/dev/null 2>&1
}
trap capture_diagnostics EXIT

echo "Waiting for Android emulator and recovering ADB if needed..."
adb wait-for-device || true
adb_ready

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
adb_retry install -r "$APK_PATH" | tee "$REPORT_DIR/install.txt"
adb_retry shell pm path "$PACKAGE_NAME" | tee -a "$REPORT_DIR/install.txt"

echo "Preparing dedicated DashCore storage inside Geode Launcher app data..."
# Geode Android stores its game and loader files under the launcher's
# app-specific external files directory. Keep a dedicated staging folder
# named DashCore there, then install the package into Geode's actual mods path.
adb_retry shell mkdir -p "$STAGING_DIR"
adb_retry push "$GEODE_PACKAGE" "$STAGING_DIR/DashCore.geode" | tee "$REPORT_DIR/mod-push.txt"
# Point Geode's normal mods directory at the dedicated DashCore folder.
# The relative symlink keeps both locations inside the launcher's app-specific storage.
adb_retry shell rm -rf "$MOD_DIR"
adb_retry shell ln -s "../../DashCore" "$MOD_DIR"
adb_retry shell test -f "$MOD_DIR/DashCore.geode"
adb_retry shell ls -ld "$MOD_DIR" | tee -a "$REPORT_DIR/mod-push.txt"
adb_retry shell ls -l "$STAGING_DIR" | tee -a "$REPORT_DIR/mod-push.txt"
echo "STAGING_DIR=$STAGING_DIR" | tee -a "$REPORT_DIR/mod-push.txt"
echo "MOD_DIR=$MOD_DIR" | tee -a "$REPORT_DIR/mod-push.txt"

echo "Launching Geometry Dash / Geode Launcher..."
adb_retry shell am force-stop "$PACKAGE_NAME" || true
adb_retry shell monkey -p "$PACKAGE_NAME" -c android.intent.category.LAUNCHER 1 | tee "$REPORT_DIR/launch.txt"

# Wait for the launcher process instead of sleeping for a fixed 15 seconds.
PID=""
for attempt in $(seq 1 20); do
  PID="$(adb shell pidof "$PACKAGE_NAME" 2>/dev/null | tr -d '\r' || true)"
  [ -n "$PID" ] && break
  sleep 1
done
if [ -z "$PID" ]; then
  echo "Launcher process did not appear after launch." | tee "$REPORT_DIR/result.txt"
  exit 1
fi

adb_retry shell am start -a android.intent.action.VIEW -d "geode-launcher://main/launch" -p "$PACKAGE_NAME" | tee -a "$REPORT_DIR/launch.txt"

# Allow startup up to 35 seconds, stopping early as soon as DashCore appears in
# either Geode log location. The final confirmation check below remains strict.
for attempt in $(seq 1 35); do
  for root in "/sdcard/Android/data/$PACKAGE_NAME/files/game/geode/logs" "/sdcard/Android/media/$PACKAGE_NAME/game/geode/logs"; do
    if adb shell "grep -R -I -q -E 'hesta\\.optgod|DashCore' '$root' 2>/dev/null"; then
      break 2
    fi
  done
  sleep 1
done

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

GEODE_LOG_ROOT_APP="/sdcard/Android/data/$PACKAGE_NAME/files/game/geode"
GEODE_LOG_ROOT_MEDIA="/sdcard/Android/media/$PACKAGE_NAME/game/geode"
adb shell "find $GEODE_LOG_ROOT_APP $GEODE_LOG_ROOT_MEDIA -maxdepth 5 -type f 2>/dev/null | head -150" > "$REPORT_DIR/geode-files.txt" || true
adb pull "$GEODE_LOG_ROOT_APP/logs" "$REPORT_DIR/geode-logs-app-files" >/dev/null 2>&1 || true
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
mod_log_confirmed = sys.argv[3] == "true"
report = {
    # Launcher startup and stability are a smoke test. DashCore loading is
    # reported independently so a live launcher cannot falsely imply the mod loaded.
    "result": "passed" if mod_log_confirmed else "inconclusive",
    "launcher_smoke_test": "passed",
    "mod_load": "confirmed" if mod_log_confirmed else "unconfirmed",
    "launcher_process": sys.argv[2],
    "dashcore_log_identifier_found": mod_log_confirmed,
    "checks": [
        "Android emulator booted",
        "Geode Launcher APK package ID verified",
        "Geode Launcher installed",
        "DashCore .geode staged in app-private DashCore folder and copied to Geode mods directory",
        "Launcher started and remained alive through the startup check",
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
    f"**Overall result: {'PASSED' if mod_log_confirmed else 'INCONCLUSIVE'}**",
    "",
    "**Launcher smoke test: PASSED**",
    f"**DashCore load: {'CONFIRMED' if mod_log_confirmed else 'NOT CONFIRMED'}**",
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

if [ "$MOD_LOG_CONFIRMED" != "true" ]; then
  echo "::error::Launcher smoke test passed, but DashCore load was not confirmed in Geode logs."
  echo "Runtime test is inconclusive; inspect the uploaded logs before treating this build as validated." >&2
  exit 2
fi

echo "Runtime smoke test completed; DashCore load was confirmed."
