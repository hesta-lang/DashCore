#!/data/data/com.termux/files/usr/bin/bash
# DashCore Kirin Profile Helper - Termux / Android
set -Eeuo pipefail

REPO="hesta-lang/DashCore"
ARTIFACT_NAME="DashCore-Kirin-CPU-Profile-Tool"
DEST="$HOME/dashcore-kirin-profile"
API="https://api.github.com/repos/$REPO/actions/artifacts?per_page=100"

say() { printf '\n[Kirin Helper] %s\n' "$*"; }
fail() { say "ERROR: $*"; exit 1; }

for cmd in curl python unzip find; do
  command -v "$cmd" >/dev/null 2>&1 || fail "Falta '$cmd'. Instálalo con: pkg install curl python unzip"
done

mkdir -p "$DEST"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say "Buscando el artefacto más reciente en GitHub Actions..."
curl -fsSL -H "Accept: application/vnd.github+json" "$API" -o "$TMP/artifacts.json" || fail "No se pudo consultar GitHub. Comprueba tu conexión."

ARTIFACT_ID="$(python - "$TMP/artifacts.json" "$ARTIFACT_NAME" <<'PY'
import json, sys
data=json.load(open(sys.argv[1], encoding="utf-8"))
items=[a for a in data.get("artifacts", []) if a.get("name")==sys.argv[2] and not a.get("expired")]
items.sort(key=lambda a: a.get("created_at",""), reverse=True)
print(items[0]["id"] if items else "")
PY
)"
[ -n "$ARTIFACT_ID" ] || fail "GitHub no encontró un artefacto vigente llamado $ARTIFACT_NAME."

say "Descargando artefacto #$ARTIFACT_ID..."
curl -fLsS -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/$REPO/actions/artifacts/$ARTIFACT_ID/zip" \
  -o "$TMP/profile-tool.zip" || fail "Falló la descarga del artefacto."

say "Comprobando y extrayendo archivos..."
unzip -oq "$TMP/profile-tool.zip" -d "$DEST" || fail "El ZIP está dañado o no se pudo extraer."

BENCH="$(find "$DEST" -type f -name kirin_cpu_bench -print -quit)"
PYTHON_SCRIPT="$(find "$DEST" -type f -name kirin_cpu_profile.py -print -quit)"
PROFILE="$(find "$DEST" -type f -name huawei-y9s.json -print -quit)"
[ -n "$BENCH" ] || fail "No aparece el ejecutable kirin_cpu_bench en el ZIP."
[ -n "$PYTHON_SCRIPT" ] || fail "No aparece kirin_cpu_profile.py en el ZIP."
[ -n "$PROFILE" ] || fail "No aparece huawei-y9s.json en el ZIP."

chmod 700 "$BENCH"
REPORT_DIR="$DEST/results"
mkdir -p "$REPORT_DIR"

DURATION_MS="${DASHCORE_CPU_DURATION_MS:-2500}"
REPETITIONS="${DASHCORE_CPU_REPETITIONS:-2}"
say "Archivos verificados. Iniciando perfil local del Huawei Y9s..."
say "Duración por prueba: ${DURATION_MS} ms; repeticiones: $REPETITIONS"

python "$PYTHON_SCRIPT" --local --binary "$BENCH" --profile "$PROFILE" \
  --report-dir "$REPORT_DIR" --duration-ms "$DURATION_MS" --repetitions "$REPETITIONS" \
  || fail "El perfilador terminó con error. Conservé los archivos descargados en $DEST."

say "Perfil terminado. Resultados:"
printf '%s\n' "$REPORT_DIR/kirin-cpu-profile.json" "$REPORT_DIR/kirin-cpu-profile.md"
say "Carpeta de trabajo: $DEST"
