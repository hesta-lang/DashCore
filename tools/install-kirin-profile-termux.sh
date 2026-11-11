#!/data/data/com.termux/files/usr/bin/bash
# DashCore Kirin Profile Helper - build directly on Android in Termux
set -Eeuo pipefail

REPO_RAW="https://raw.githubusercontent.com/hesta-lang/DashCore/main"
DEST="$HOME/dashcore-kirin-profile"
SOURCE_DIR="$DEST/source"
REPORT_DIR="$DEST/results"

say() { printf '\n[Kirin Helper] %s\n' "$*"; }
fail() { say "ERROR: $*"; exit 1; }

for cmd in curl python clang; do
  command -v "$cmd" >/dev/null 2>&1 || fail "Falta '$cmd'. Instálalo con: pkg install curl python clang"
done

mkdir -p "$SOURCE_DIR" "$REPORT_DIR"

say "Descargando fuentes públicas de DashCore (sin usar GitHub Actions artifacts)..."
curl -fLsS "$REPO_RAW/tools/kirin_cpu_bench.c" -o "$SOURCE_DIR/kirin_cpu_bench.c" || fail "No se pudo descargar kirin_cpu_bench.c."
curl -fLsS "$REPO_RAW/scripts/kirin_cpu_profile.py" -o "$SOURCE_DIR/kirin_cpu_profile.py" || fail "No se pudo descargar kirin_cpu_profile.py."
curl -fLsS "$REPO_RAW/profiles/huawei-y9s.json" -o "$SOURCE_DIR/huawei-y9s.json" || fail "No se pudo descargar huawei-y9s.json."

say "Compilando el benchmark nativo para la arquitectura de este teléfono..."
clang -O2 -std=c11 -pthread "$SOURCE_DIR/kirin_cpu_bench.c" -o "$DEST/kirin_cpu_bench" || fail "Falló la compilación. Envía el error completo de clang."
chmod 700 "$DEST/kirin_cpu_bench"

say "Verificando arquitectura y archivos..."
uname -m
file "$DEST/kirin_cpu_bench" 2>/dev/null || true
python -m py_compile "$SOURCE_DIR/kirin_cpu_profile.py" || fail "El script Python no pasó la comprobación de sintaxis."

DURATION_MS="${DASHCORE_CPU_DURATION_MS:-2500}"
REPETITIONS="${DASHCORE_CPU_REPETITIONS:-2}"
say "Ejecutando perfil local: ${DURATION_MS} ms por prueba, $REPETITIONS repeticiones."
python "$SOURCE_DIR/kirin_cpu_profile.py" --local \
  --binary "$DEST/kirin_cpu_bench" \
  --profile "$SOURCE_DIR/huawei-y9s.json" \
  --report-dir "$REPORT_DIR" \
  --duration-ms "$DURATION_MS" \
  --repetitions "$REPETITIONS" || fail "El perfilador terminó con error. Los archivos permanecen en $DEST."

say "Perfil completado. Informes generados:"
printf '%s\n' "$REPORT_DIR/kirin-cpu-profile.json" "$REPORT_DIR/kirin-cpu-profile.md"
say "Carpeta de trabajo: $DEST"
