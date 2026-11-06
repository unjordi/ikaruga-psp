#!/usr/bin/env bash
# Acceptance tests of fila 5c (docs/geometria-psp.md):
#   (a) at guest frame N (default 2600) the PSP capture has geometry (> 20 % of the screen off the
#       black clear colour) and its mask matches PC's with IoU >= 0.6;
#   (b) negative: with drawing off (ikaruga.cfg dibujar=0) the capture is one colour and
#       comparar-geometria.sh fails;
#   (c) the polygon count of the decoded Frame at frame N is the same on PSP and on PC.
#   scripts/test-psp-geometria.sh <EBOOT.PBP> <dir-juego-extraido> <ikaruga_boot> <ikaruga.toml>
#                                 [--cuadro N] [--reusar]
# Each PPSSPP run of frame 2600 takes about 30 min; the two runs are started in parallel.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMP="$REPO/scripts/comparar-geometria.sh"
args=() extra=() cuadro=2600
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; extra+=(--cuadro "$2"); shift 2 ;;
    --reusar) extra+=(--reusar); shift ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 4 ]] || { sed -n '7,8p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
mkdir -p "$EV"

set +e
"$CMP" "${args[@]}" "${extra[@]}" > "$EV/test-geometria-$cuadro.out" 2>&1 &
p1=$!
"$CMP" "${args[@]}" "${extra[@]}" --sin-dibujo > "$EV/test-geometria-$cuadro-sin-dibujo.out" 2>&1 &
p2=$!
wait $p1; rc_dibujo=$?
wait $p2; rc_sin=$?
set -e

fail=0
res="$EV/geometria-$cuadro/resultado.txt"
neg="$EV/geometria-$cuadro-sin-dibujo/resultado.txt"
cat "$res" 2>/dev/null || true
if [[ $rc_dibujo -eq 0 ]] && grep -q '^PASS' "$res"; then
  echo "(a) ok: geometry and IoU"
else
  echo "(a) FAIL: see $res"; fail=1
fi
if [[ $rc_sin -ne 0 ]] && grep -q ', 1 colours' "$neg" 2>/dev/null; then
  echo "(b) ok: without drawing the capture is one colour and the comparison fails"
else
  echo "(b) FAIL: the run without drawing did not give a one-colour capture that fails (see $neg)"
  fail=1
fi
if grep -Eq '^polygons psp ([0-9]+) pc \1$' "$res" 2>/dev/null; then
  echo "(c) ok: $(grep '^polygons' "$res")"
else
  echo "(c) FAIL: $(grep '^polygons' "$res" 2>/dev/null || echo 'no counts')"; fail=1
fi
exit $fail
