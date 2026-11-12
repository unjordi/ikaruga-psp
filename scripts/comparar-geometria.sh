#!/usr/bin/env bash
# Compares the geometry the PSP EBOOT draws at one guest frame with what Flycast draws in the same
# scene (a window of the Flycast reference series), with the same scripted presses.
#   scripts/comparar-geometria.sh <EBOOT.PBP> <dir-juego-extraido> [--cuadro N] [--ventana F0-F1]
#                                 [--sin-dibujo] [--salida DIR] [--reusar] [--segundos S] [--serie DIR]
# PSP: PPSSPP runs the EBOOT with ikaruga.cfg = capturar_en=N, pulsar=<P> and rtc_semilla=1 (and
# dibujar=0 with --sin-dibujo); the EBOOT writes captura-N.raw (480x272 RGB888) and quits.
# Flycast: scripts/comparar-flycast.sh --captura, mask = pixels with any channel above 24 (the
# clear colour is black), the Flycast image being the one of the window with the highest SSIM.
# Passes (exit 0) when the PSP mask covers > 20 % of the screen and IoU(PSP, Flycast) >= 0.6;
# exit 1 otherwise, 3 when the scene is not in the window.
# Default N: 2350 (start of chapter 1); default window: scripts/escenas-flycast.sh (VENTANA_CAP1).
# Default DIR: build/psp/evidencia/geometria-N[-sin-dibujo]. resultado.txt has the numbers and
# "psp: N colours".
# Env: PROBAR_DISCO as in scripts/probar-ikaruga-ppsspp.sh. Host needs: what comparar-titulo.sh needs.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/scripts/escenas-flycast.sh"
cuadro="$CUADRO_CAP1" ventana="$VENTANA_CAP1" sin_dibujo=0 salida="" pass=()
pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --ventana) ventana="$2"; shift 2 ;;
    --sin-dibujo) sin_dibujo=1; shift ;;
    --salida) salida="$2"; shift 2 ;;
    --reusar) pass+=(--reusar); shift ;;
    --segundos|--serie) pass+=("$1" "$2"); shift 2 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 2 ]] || { sed -n '3,4p' "$0" >&2; exit 2; }
[[ "$cuadro" =~ ^[0-9]+$ ]] || { echo "--cuadro must be a number" >&2; exit 2; }
sufijo=""; [[ $sin_dibujo -eq 1 ]] && sufijo="-sin-dibujo"
OUT="$(realpath -m "${salida:-$REPO/build/psp/evidencia/geometria-$cuadro$sufijo}")"
cfg=()
[[ $sin_dibujo -eq 1 ]] && cfg=(--cfg dibujar=0 --sin-borde)

set +e
"$REPO/scripts/comparar-titulo.sh" "${pos[@]}" --cuadro "$cuadro" --pulsar "$PRESSES_CAP1" \
  --ventana "$ventana" --salida "$OUT" --iou --exigir 'iou>=0.6' "${cfg[@]}" "${pass[@]}" \
  > "$OUT.out" 2>&1
rc=$?
set -e
[[ -s "$OUT/captura-$cuadro.raw" ]] || { cat "$OUT.out" >&2; exit 1; }
python3 - "$OUT/captura-$cuadro.raw" >> "$OUT/resultado.txt" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
print(f"psp: {len(set(zip(data[0::3], data[1::3], data[2::3])))} colours")
PY
cat "$OUT/resultado.txt"
# comparar-flycast.sh's verdict plus the coverage floor: a capture that is almost all background
# has no geometry to compare.
cobertura="$(sed -n 's/.*geometria psp \([0-9.]*\) .*/\1/p' "$OUT/resultado.txt")"
if [[ $rc -eq 0 ]] && ! python3 -c "import sys; sys.exit(0 if float('${cobertura:-0}') > 0.20 else 1)"; then
  echo "FAIL: PSP geometry covers ${cobertura:-0} <= 0.20"; exit 1
fi
exit $rc
