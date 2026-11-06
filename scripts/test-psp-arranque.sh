#!/usr/bin/env bash
# Acceptance test for the headless PSP boot, in PPSSPP:
#  - without game data the EBOOT logs a clear error about 1ST_READ.BIN and starts no guest;
#  - with the extracted disc the guest advances: frames > 0 and its PC takes several values.
# Prints the last log line (frames, guest/host seconds, speed in times real time).
#   scripts/test-psp-arranque.sh <EBOOT.PBP> <dir-juego-extraido> [segundos (default 60)]
# Logs go to build/psp/evidencia/test-arranque-{sin-juego,juego}.log.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -lt 2 || $# -gt 3 ]]; then
  sed -n '5p' "$0" >&2
  exit 2
fi
EBOOT="$1" JUEGO="$2" SEGUNDOS="${3:-60}"
OUT="$REPO/build/psp/evidencia"
mkdir -p "$OUT"
fail() { echo "FAIL: $*" >&2; exit 1; }

echo "== negative: no 1ST_READ.BIN"
neg="$OUT/test-arranque-sin-juego.log"
"$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" - 10 "$neg" >/dev/null
grep -q '^error: cannot read ms0:/PSP/GAME/IKARUGA/1ST_READ.BIN' "$neg" ||
  fail "no clear error in $neg"
! grep -q '^tick' "$neg" || fail "the guest ran without a program ($neg)"
echo "ok: $(grep '^error' "$neg")"

echo "== positive: $SEGUNDOS s with the game"
pos="$OUT/test-arranque-juego.log"
"$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" "$SEGUNDOS" "$pos" >/dev/null
! grep -q '^fault\|^stop' "$pos" || fail "the guest stopped: $(grep '^fault\|^stop' "$pos")"
last="$(grep '^tick' "$pos" | tail -1)"
[[ -n "$last" ]] || fail "no tick line in $pos"
frames="$(awk '{print $3}' <<<"$last")"
pcs="$(grep '^tick' "$pos" | awk '{print $NF}' | sort -u | wc -l)"
(( frames > 0 )) || fail "no guest frames: $last"
(( pcs >= 2 )) || fail "the guest PC never changed ($pcs distinct values)"
echo "ok: $last"
echo "ok: $pcs distinct guest PCs over $(grep -c '^tick' "$pos") log lines"
