#!/usr/bin/env bash
# The EBOOT finds its files next to itself, wherever it is installed: installs it under
# ms0:/PSP/GAME/OTRA_CARPETA/ (not IKARUGA) in PPSSPP and requires the guest to run from there.
#   scripts/test-psp-carpeta.sh <EBOOT.PBP> <dir-juego-extraido> [segundos (default 30)]
# Log: build/psp/evidencia/test-carpeta.log.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 2 ]] || { sed -n '4p' "$0" >&2; exit 2; }
log="$REPO/build/psp/evidencia/test-carpeta.log"
rm -f "$log"
PROBAR_CARPETA=OTRA_CARPETA "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$1" "$2" "${3:-30}" "$log" \
  >/dev/null 2>&1 || true
[[ -s "$log" ]] || { echo "FAIL: nothing written in ms0:/PSP/GAME/OTRA_CARPETA/" >&2; exit 1; }
grep -q '^tick' "$log" || {
  echo "FAIL: the guest did not run from OTRA_CARPETA:" >&2; cat "$log" >&2; exit 1; }
echo "ok: $(grep '^tick' "$log" | tail -1)"
