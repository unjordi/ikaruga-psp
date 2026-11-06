#!/usr/bin/env bash
# The memory card survives between runs: two PPSSPP runs in a row on the same game. The first
# starts without VMU.BIN and must leave a 128 KB one (Ikaruga writes its save around frame 206);
# the second starts with that file and must log that it read it, and leave it a valid card.
#   scripts/test-psp-vmu.sh <EBOOT.PBP> <dir-juego-extraido> [segundos por corrida (default 100)]
# Env: PROBAR_DISCO as in scripts/probar-ikaruga-ppsspp.sh.
# Logs and cards: build/psp/evidencia/vmu/{corrida1,corrida2}.{log,vmu}.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 2 ]] || { sed -n '5p' "$0" >&2; exit 2; }
OUT="$REPO/build/psp/evidencia/vmu"
rm -rf "$OUT"
mkdir -p "$OUT"
SEG="${3:-100}"
fail() { echo "FAIL: $*" >&2; exit 1; }

echo "== run 1: no VMU.BIN"
PROBAR_VMU_OUT="$OUT/corrida1.vmu" "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$1" "$2" "$SEG" \
  "$OUT/corrida1.log" >/dev/null 2>&1 || true
[[ -s "$OUT/corrida1.log" ]] || fail "run 1 wrote no log"
[[ -f "$OUT/corrida1.vmu" ]] || fail "run 1 left no VMU.BIN (last: $(tail -1 "$OUT/corrida1.log"))"
grep -q '^memory card: no VMU.BIN' "$OUT/corrida1.log" || fail "run 1 did not start blank"
size="$(stat -c %s "$OUT/corrida1.vmu")"
[[ "$size" == 131072 ]] || fail "run 1 left a VMU.BIN of $size bytes"
echo "ok: run 1 saved VMU.BIN, $(md5sum < "$OUT/corrida1.vmu" | cut -c1-32)"

echo "== run 2: with run 1's VMU.BIN"
PROBAR_VMU_IN="$OUT/corrida1.vmu" PROBAR_VMU_OUT="$OUT/corrida2.vmu" \
  "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$1" "$2" "$SEG" "$OUT/corrida2.log" >/dev/null 2>&1 || true
[[ -s "$OUT/corrida2.log" ]] || fail "run 2 wrote no log"
grep -q '^memory card: read VMU.BIN' "$OUT/corrida2.log" || fail "run 2 did not read the card"
grep -q '^tick' "$OUT/corrida2.log" || fail "run 2 did not run the guest"
[[ "$(stat -c %s "$OUT/corrida2.vmu" 2>/dev/null)" == 131072 ]] || fail "run 2 left no valid card"
echo "ok: run 2 read it and left $(md5sum < "$OUT/corrida2.vmu" | cut -c1-32)"
