#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DURATION_MS="$DASHCORE_CPU_DURATION_MS"
REPETITIONS="$DASHCORE_CPU_REPETITIONS"
[ -n "$DURATION_MS" ] || DURATION_MS=2500
[ -n "$REPETITIONS" ] || REPETITIONS=2

if ! command -v python3 >/dev/null 2>&1; then
  echo "Python 3 is required. In Termux run: pkg install python"
  exit 1
fi
[ -x "$ROOT/kirin_cpu_bench" ] || chmod 700 "$ROOT/kirin_cpu_bench"

python3 "$ROOT/kirin_cpu_profile.py" \
  --local --binary "$ROOT/kirin_cpu_bench" \
  --profile "$ROOT/huawei-y9s.json" \
  --report-dir "$ROOT/results" \
  --duration-ms "$DURATION_MS" --repetitions "$REPETITIONS"

tar -czf "$ROOT/kirin-cpu-profile-results.tar.gz" \
  -C "$ROOT/results" kirin-cpu-profile.json kirin-cpu-profile.md
echo "Profile saved to: $ROOT/results/kirin-cpu-profile.json"
echo "Share this archive: $ROOT/kirin-cpu-profile-results.tar.gz"
