#!/usr/bin/env bash
# Acceptance tests of fila 5c (docs/geometria-psp.md), against the Flycast reference series
# (docs/referencia-flycast.md):
#   (a) at guest frame N (default 2350) the PSP capture has geometry (> 20 % of the screen off the
#       black clear colour) and the IoU of its mask with Flycast's is >= 0.6;
#   (b) negative: with drawing off (ikaruga.cfg dibujar=0) the capture is one colour and its IoU is
#       below 0.6.
#   scripts/test-psp-geometria.sh <EBOOT.PBP> <dir-juego-extraido> [--cuadro N] [--reusar]
# The two PPSSPP runs are sequential (unthrottled, each about 35 s).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMP="$REPO/scripts/comparar-geometria.sh"
. "$REPO/scripts/escenas-flycast.sh"
args=() extra=() cuadro="$CUADRO_CAP1"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; extra+=(--cuadro "$2"); shift 2 ;;
    --reusar) extra+=(--reusar); shift ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 2 ]] || { sed -n '9p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
mkdir -p "$EV"

set +e
"$CMP" "${args[@]}" "${extra[@]}" > "$EV/test-geometria-$cuadro.out" 2>&1
rc_dibujo=$?
"$CMP" "${args[@]}" "${extra[@]}" --sin-dibujo > "$EV/test-geometria-$cuadro-sin-dibujo.out" 2>&1
rc_sin=$?
set -e

fail=0
res="$EV/geometria-$cuadro/resultado.txt"
neg="$EV/geometria-$cuadro-sin-dibujo/resultado.txt"
cat "$res" 2>/dev/null || true
if [[ $rc_dibujo -eq 0 ]] && grep -q '^PASS' "$res"; then
  echo "(a) ok: geometry and IoU $(sed -n 's/^  iou \([0-9.]*\).*/\1/p' "$res") >= 0.6"
else
  echo "(a) FAIL (exit $rc_dibujo): see $res"; fail=1
fi
if [[ $rc_sin -eq 1 ]] && grep -q '^psp: 1 colours' "$neg" 2>/dev/null; then
  echo "(b) ok: without drawing the capture is one colour and the IoU is $(sed -n 's/^  iou \([0-9.]*\).*/\1/p' "$neg") < 0.6"
else
  echo "(b) FAIL (exit $rc_sin): the run without drawing did not give a one-colour capture below the IoU (see $neg)"
  fail=1
fi
exit $fail
