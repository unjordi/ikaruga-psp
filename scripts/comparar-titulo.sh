#!/usr/bin/env bash
# Runs the PSP EBOOT in PPSSPP up to one guest frame, takes the EBOOT's own capture of that frame
# (captura-N.raw, 480x272 RGB888) and compares it with the Flycast reference series
# (scripts/comparar-flycast.sh --captura). The PC launcher is never run.
#   scripts/comparar-titulo.sh <EBOOT.PBP> <dir-juego-extraido> [--cuadro N] [--ventana F0-F1]
#                              [--sin-texturas] [--salida DIR] [--reusar] [--segundos S]
#                              [--pulsar P] [--cfg LINE]... [--serie DIR] [--region NOMBRE:x0,y0,x1,y1]...
#                              [--exigir REGLA]... [--iou] [--sin-borde] [--alineado [--desfase-var NAME]]
# PSP: PPSSPP runs the EBOOT with ikaruga.cfg = capturar_en=N, pulsar=P (default start@700),
# rtc_semilla=1 (and texturas=0 with --sin-texturas); --cfg LINE adds LINE to it (repeatable).
# Default N: 1243, the first frame of the title that is identical to the settled title
# (frames 1210-1242 fade in). Default window: the title's (scripts/escenas-flycast.sh).
# --alineado: compare with Flycast's images aligned with frame N (scripts/flycast-alineado.sh, the
# same presses) instead of a window of the series. With presses other than PRESSES_CAP1 the offset
# variable must be named with --desfase-var (scripts/escenas-flycast.sh).
# --ventana, --serie, --region, --exigir, --iou and --sin-borde go to comparar-flycast.sh as they are; without
# --exigir it only prints the numbers (exit 0). Exit: comparar-flycast.sh's (0 ok, 1 a rule fails,
# 3 scene not found), 2 on usage errors, 1 when PPSSPP left no capture.
# Default DIR: build/psp/evidencia/titulo-N[-sin-texturas]. --reusar keeps an existing capture.
# Env: PROBAR_DISCO as in scripts/probar-ikaruga-ppsspp.sh; PROBAR_SIN_LIMITE defaults to 1 here
# (PPSSPP unthrottled, docs/pruebas-rapidas.md), PROBAR_SIN_LIMITE=0 runs at 60 Hz.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/scripts/escenas-flycast.sh"
PRESSES="start@700"
cuadro=1243 ventana="$VENTANA_TITULO" sin_texturas=0 salida="" reusar=0 segundos=""
pos=() extra_cfg=() cmp_args=() alineado=0 desfase_var=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --ventana) ventana="$2"; shift 2 ;;
    --sin-texturas) sin_texturas=1; shift ;;
    --salida) salida="$2"; shift 2 ;;
    --reusar) reusar=1; shift ;;
    --segundos) segundos="$2"; shift 2 ;;
    --pulsar) PRESSES="$2"; shift 2 ;;
    --cfg) extra_cfg+=("$2"); shift 2 ;;
    --serie|--region|--exigir) cmp_args+=("$1" "$2"); shift 2 ;;
    --iou|--sin-borde) cmp_args+=("$1"); shift ;;
    --alineado) alineado=1; shift ;;
    --desfase-var) desfase_var="$2"; shift 2 ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 2 ]] || { sed -n '3,6p' "$0" >&2; exit 2; }
EBOOT="$(realpath "${pos[0]}")" JUEGO="$(realpath "${pos[1]}")"
[[ "$cuadro" =~ ^[0-9]+$ ]] || { echo "--cuadro must be a number" >&2; exit 2; }
sufijo=""
[[ $sin_texturas -eq 1 ]] && sufijo="-sin-texturas"
OUT="$(realpath -m "${salida:-$REPO/build/psp/evidencia/titulo-$cuadro$sufijo}")"
[[ -n "$segundos" ]] || segundos=$(( cuadro * 2 + 600 ))
mkdir -p "$OUT"

# PSP.
PSP_RAW="$OUT/captura-$cuadro.raw" PSP_LOG="$OUT/psp.log"
if [[ $reusar -eq 0 || ! -s "$PSP_RAW" ]]; then
  rm -f "$PSP_RAW"
  {
    echo "capturar_en=$cuadro"
    echo "pulsar=$PRESSES"
    echo "rtc_semilla=1"
    # Video off until 10 frames before the capture (docs/pruebas-rapidas.md); a later
    # dibujar_desde=0 line (--cfg) draws every frame again.
    (( cuadro > 10 )) && echo "dibujar_desde=$(( cuadro - 10 ))"
    [[ $sin_texturas -eq 1 ]] && echo "texturas=0"
    for l in "${extra_cfg[@]}"; do echo "$l"; done
  } > "$OUT/ikaruga.cfg"
  start=$(date +%s)
  PROBAR_SIN_LIMITE="${PROBAR_SIN_LIMITE:-1}" PROBAR_CFG="$OUT/ikaruga.cfg" \
    "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" \
    "$segundos" "$PSP_LOG" > "$OUT/probar.out"
  echo "psp: PPSSPP wall time $(( $(date +%s) - start )) s" | tee "$OUT/psp-tiempo.txt"
fi
[[ -s "$PSP_RAW" ]] || { echo "comparar-titulo: no PSP capture (see $PSP_LOG)" >&2; exit 1; }

# Flycast.
if [[ $alineado -eq 1 ]]; then
  if [[ "$PRESSES" != "$PRESSES_CAP1" && -z "$desfase_var" ]]; then
    echo "comparar-titulo: --alineado with presses $PRESSES needs --desfase-var" >&2; exit 2
  fi
  fa=(--pulsar "$PRESSES"); [[ -n "$desfase_var" ]] && fa+=(--desfase-var "$desfase_var")
  imgs=()
  while IFS= read -r f; do imgs+=(--imagen "$f"); done < <("$REPO/scripts/flycast-alineado.sh" "$cuadro" "${fa[@]}")
  [[ ${#imgs[@]} -gt 0 ]] || { echo "comparar-titulo: no aligned Flycast images for frame $cuadro" >&2; exit 2; }
  "$REPO/scripts/comparar-flycast.sh" --captura "$PSP_RAW" "${imgs[@]}" --salida "$OUT" "${cmp_args[@]}"
else
  "$REPO/scripts/comparar-flycast.sh" --captura "$PSP_RAW" --ventana "$ventana" --salida "$OUT" \
    "${cmp_args[@]}"
fi
