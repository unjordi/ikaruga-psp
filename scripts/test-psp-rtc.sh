#!/usr/bin/env bash
# The console clock is seeded from ikaruga.cfg, not from the host's time: writes rtc_semilla = 1000
# into ikaruga.cfg, runs the EBOOT in PPSSPP and requires the AICA RTC to read
# 1000 + 631152000 (1950->1970) + 1 at frame 60 (one tick every 200M guest cycles, as on PC).
#   scripts/test-psp-rtc.sh <EBOOT.PBP> <dir-juego-extraido> [segundos (default 30)]
# Log: build/psp/evidencia/test-rtc.log.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 2 ]] || { sed -n '5p' "$0" >&2; exit 2; }
OUT="$REPO/build/psp/evidencia"
mkdir -p "$OUT"
log="$OUT/test-rtc.log"
rm -f "$log"
printf '# test\nrtc_semilla = 1000\n' >"$OUT/test-rtc.cfg"
PROBAR_CFG="$OUT/test-rtc.cfg" "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$1" "$2" "${3:-30}" "$log" \
  >/dev/null 2>&1 || true
[[ -s "$log" ]] || { echo "FAIL: the EBOOT wrote no log" >&2; exit 1; }
esperado=$((1000 + 631152000 + 1))
got="$(awk '$1 == "hash" && $2 == 60 { print $7; exit }' "$log")"
[[ -n "$got" ]] || { echo "FAIL: no RTC reading at frame 60 in $log" >&2; exit 1; }
[[ "$got" == "$esperado" ]] || { echo "FAIL: RTC at frame 60 is $got, expected $esperado" >&2; exit 1; }
echo "ok: RTC at frame 60 = $got (seed 1000)"
