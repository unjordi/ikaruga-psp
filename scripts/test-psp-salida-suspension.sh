#!/usr/bin/env bash
# HOME and suspend, in PPSSPP, through the debug keys of ikaruga.cfg:
#  - test_exit_at = 120 notifies the registered exit callback (what HOME -> Exit does): the EBOOT
#    must leave the guest at that frame or the next ("stop: exit requested") and log nothing after;
#  - test_suspend_at = 30 sends the suspend/resume power events and closes the disc's descriptor
#    in between, as the kernel does: the guest must keep reading the disc and its store hash must
#    equal PC's at frames 60..240 (RTC seed 1).
#   scripts/test-psp-salida-suspension.sh <EBOOT.PBP> <dir-juego-extraido>
# Env: DREAM_RECOMP, PROBAR_DISCO as in scripts/test-psp-igual-pc.sh.
# Logs: build/psp/evidencia/salida-suspension/.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -eq 2 ]] || { sed -n '8p' "$0" >&2; exit 2; }
OUT="$REPO/build/psp/evidencia/salida-suspension"
rm -rf "$OUT"
mkdir -p "$OUT"
fallos=0
mal() { echo "FAIL: $*"; fallos=$((fallos + 1)); }

echo "== exit callback at frame 120"
printf 'rtc_seed = 1\ntest_exit_at = 120\n' >"$OUT/salida.cfg"
PROBAR_CFG="$OUT/salida.cfg" "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$1" "$2" 90 \
  "$OUT/salida.log" >"$OUT/salida.out" 2>"$OUT/salida.err" || true
# The callback runs at the next reschedule, so the guest may finish the frame it is in. PPSSPP
# stays open at its menu after sceKernelExitGame, so the log is what tells: a stop line and nothing
# after it.
stop_frame="$(awk '/^stop: exit requested/ { print $5; exit }' "$OUT/salida.log" 2>&1)"
if [[ "$stop_frame" =~ ^12[01]$ ]] && [[ "$(tail -1 "$OUT/salida.log")" == stop:* ]]; then
  echo "ok: $(grep '^stop' "$OUT/salida.log")"
else
  mal "no clean exit at frame 120: $(tail -1 "$OUT/salida.log" 2>&1); $(tail -1 "$OUT/salida.err")"
fi

echo "== suspend and resume at frame 30"
printf 'rtc_seed = 1\ntest_suspend_at = 30\n' >"$OUT/suspension.cfg"
if IGUAL_OUT="$OUT/igual-pc" HASTA=240 RTC_SEED=1 PROBAR_CFG_EXTRA="$OUT/suspension.cfg" \
   "$REPO/scripts/test-psp-igual-pc.sh" "$1" "$2" 150 >"$OUT/suspension.txt" 2>&1; then
  echo "ok: $(tail -1 "$OUT/suspension.txt")"
else
  mal "after the resume: $(tail -1 "$OUT/suspension.txt")"
fi
[[ $fallos -eq 0 ]] && echo "all passed" || { echo "$fallos failed"; exit 1; }
