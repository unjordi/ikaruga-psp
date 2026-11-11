#!/usr/bin/env bash
# medir-velocidad.sh <EBOOT.PBP> <dir-juego-extraido> [--veces N] [--salida DIR] [--solo 2600|1243]
# Speed of the EBOOT in PPSSPP at two fixed points, N runs each (default 2):
#   juego:  capturar_en=2600 with the presses of test-psp-geometria.sh (chapter 1)
#   titulo: capturar_en=1243 pulsar=start@700 (title screen)
# Prints one line per run: case, run, speed (from the "capture written" line), host_s,
# draw_ms_per_render (last video line), texture counters (last textures line), wall seconds.
# Needs PROBAR_DISCO (real disc HD area). Logs go to DIR (default build/psp/evidencia/velocidad).
# Only one PPSSPP at a time: the runs are sequential.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
veces=2 salida="" solo=""
pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --veces) veces="$2"; shift 2 ;;
    --salida) salida="$2"; shift 2 ;;
    --solo) solo="$2"; shift 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 2 ]] || { sed -n '2p' "$0" >&2; exit 2; }
[[ -n "${PROBAR_DISCO:-}" ]] || { echo "medir-velocidad: set PROBAR_DISCO" >&2; exit 2; }
EBOOT="$(realpath "${pos[0]}")" JUEGO="$(realpath "${pos[1]}")"
OUT="$(realpath -m "${salida:-$REPO/build/psp/evidencia/velocidad}")"
mkdir -p "$OUT"

campo() {  # campo <line> <key>: value following <key> in a "key value" log line
  awk -v k="$2" '{for (i = 1; i < NF; i++) if ($i == k) { print $(i + 1); exit }}' <<<"$1"
}

correr() {  # correr <caso> <cuadro> <pulsar> <segundos> <vez>
  local caso="$1" cuadro="$2" pulsar="$3" seg="$4" vez="$5"
  local dir="$OUT/$caso-$vez" cfg log
  rm -rf "$dir"; mkdir -p "$dir"
  cfg="$dir/ikaruga.cfg" log="$dir/psp.log"
  { echo "capturar_en=$cuadro"; echo "pulsar=$pulsar"; echo "rtc_seed=1"; } > "$cfg"
  local t0; t0=$(date +%s)
  # Speed is measured with PPSSPP's 60 Hz limiter on, whatever the caller exported.
  PROBAR_SIN_LIMITE=0 PROBAR_CFG="$cfg" "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" "$seg" "$log" \
    >/dev/null 2>&1 || true
  local wall=$(( $(date +%s) - t0 ))
  local fin vid tex
  fin="$(grep -a 'capture written' "$log" | tail -1 || true)"
  vid="$(grep -a '^video: \|video: renders' "$log" | tail -1 || true)"
  tex="$(grep -a 'textures: ' "$log" | tail -1 || true)"
  local speed host_s draw
  speed="$(campo "$fin" speed)"; host_s="$(campo "$fin" host_s)"
  draw="$(campo "$vid" draw_ms_per_render)"
  printf '%-6s run %s  cuadro %s  speed %s  host_s %s  draw_ms_per_render %s  wall_s %s\n' \
    "$caso" "$vez" "$cuadro" "${speed:-FALLO}" "${host_s:--}" "${draw:--}" "$wall"
  [[ -n "$tex" ]] && echo "       ${tex#*textures: }"
  grep -a 'perf: ' "$log" | tail -1 | sed 's/^.*perf: /       perf: /' || true
}

{
  echo "# medir-velocidad $(date -u +%FT%TZ) eboot $(sha256sum "$EBOOT" | cut -c1-12)"
  for vez in $(seq 1 "$veces"); do
    [[ -z "$solo" || "$solo" == 2600 ]] && correr juego 2600 "$PRESSES" 2400 "$vez"
    [[ -z "$solo" || "$solo" == 1243 ]] && correr titulo 1243 "start@700" 900 "$vez"
  done
} | tee "$OUT/resumen.txt"
