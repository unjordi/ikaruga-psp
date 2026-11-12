#!/usr/bin/env bash
# medir-memoria.sh <EBOOT.PBP> <dir-juego-extraido> [--salida DIR] [--texturas-kb N] [--solo titulo|2350|2600]
# Memory counter of the EBOOT (ikaruga.cfg memoria=1, docs/memoria-psp.md) in PPSSPP without the
# 60 Hz limiter, at the title (capturar_en=1243, start@700) and in chapter 1 (2350 and 2600, the
# presses of medir-velocidad.sh). Prints the "mem: capture", "memory:" and "textures:" lines of
# each run; the logs are DIR/<punto>/psp.log (default build/psp/evidencia/memoria).
# Needs PROBAR_DISCO. Sequential: one PPSSPP at a time.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
salida="" kb="" solo=""
pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --salida) salida="$2"; shift 2 ;;
    --texturas-kb) kb="$2"; shift 2 ;;
    --solo) solo="$2"; shift 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 2 ]] || { sed -n '2p' "$0" >&2; exit 2; }
[[ -n "${PROBAR_DISCO:-}" ]] || { echo "medir-memoria: set PROBAR_DISCO" >&2; exit 2; }
EBOOT="$(realpath "${pos[0]}")" JUEGO="$(realpath "${pos[1]}")"
OUT="$(realpath -m "${salida:-$REPO/build/psp/evidencia/memoria}")"

correr() {  # correr <punto> <cuadro> <pulsar> <segundos>
  local punto="$1" cuadro="$2" pulsar="$3" seg="$4" dir="$OUT/$1"
  rm -rf "$dir"; mkdir -p "$dir"
  { echo "capturar_en=$cuadro"; echo "pulsar=$pulsar"; echo "rtc_seed=1"; echo "memoria=1"
    [[ -n "$kb" ]] && echo "texturas_kb=$kb"; } > "$dir/ikaruga.cfg"
  PROBAR_SIN_LIMITE=1 PROBAR_CFG="$dir/ikaruga.cfg" \
    "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" "$seg" "$dir/psp.log" >/dev/null 2>&1 || true
  echo "== $punto (cuadro $cuadro${kb:+, texturas_kb $kb})"
  grep -a '^mem: \(start\|system\|program\|setup\)\|^heap-at-start\|^mem: capture\|^memory:\|capture written' "$dir/psp.log" \
    | sed 's/ guest_s .*speed/ ... speed/' || echo "FALLO: sin líneas"
  grep -a '^textures: textured' "$dir/psp.log" | tail -1 || true
}

{
  echo "# medir-memoria $(date -u +%FT%TZ) eboot $(sha256sum "$EBOOT" | cut -c1-12)"
  [[ -z "$solo" || "$solo" == titulo ]] && correr titulo 1243 "start@700" 900
  [[ -z "$solo" || "$solo" == 2350 ]] && correr juego-2350 2350 "$PRESSES" 900
  [[ -z "$solo" || "$solo" == 2600 ]] && correr juego-2600 2600 "$PRESSES" 900
} | tee "$OUT.resumen.txt"
! grep -q FALLO "$OUT.resumen.txt"
