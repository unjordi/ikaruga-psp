#!/usr/bin/env bash
# Fila 8b acceptance: with FIPR/FTRV/FMAC in double on both sides (EBOOT built with
# DREAM_FPU_DOUBLE=1, PC launcher double) and the chapter-1 presses, write_hash must be equal at
# frames 60..2600 (every 60). The EBOOT logs a hash every frame, so on a failure this also names
# the first different frame (1983 without patch 0010's soft DAZ, docs/correctitud-psp.md).
#   scripts/test-psp-igual-pc-double.sh <EBOOT.PBP> <dir-juego-extraido> [segundos (default 1500)]
#   scripts/test-psp-igual-pc-double.sh --contraprueba <EBOOT sin DAZ> <dir-juego> [segundos]
#     expects the failure instead: passes only if the first different frame is 1983.
# Build the EBOOTs with:
#   DREAM_FPU_DOUBLE=1 scripts/build-psp-ikaruga.sh <gen> --variantes=mix              (fix)
#   DREAM_FPU_DOUBLE=1 DREAM_SOFT_DAZ=0 scripts/build-psp-ikaruga.sh <gen> --variantes=mix (reverted)
# Env: DREAM_RECOMP (double PC launcher), PROBAR_DISCO, IGUAL_OUT: as test-psp-igual-pc.sh.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
contra=0
[[ "${1:-}" == --contraprueba ]] && { contra=1; shift; }
[[ $# -ge 2 && $# -le 3 ]] || { sed -n '6,8p' "$0" >&2; exit 2; }
OUT="${IGUAL_OUT:-$REPO/build/psp/evidencia/igual-pc-double}"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
EXPECTED=1983
mkdir -p "$OUT"
printf 'hash_every=1\n' >"$OUT.cfg"

rc=0
IGUAL_OUT="$OUT" PROBAR_CFG_EXTRA="$OUT.cfg" PULSAR="$PRESSES" HASTA=2600 \
  "$REPO/scripts/test-psp-igual-pc.sh" "$1" "$2" "${3:-1500}" || rc=$?

# First different frame over every frame both runs logged.
first="$(join <(awk '!/^#/{print $1, $3}' "$OUT/pc-hash.txt" | sort -k1,1 -u) \
              <(awk '$1 == "hash" {print $2, $4}' "$OUT/psp.log" | sort -k1,1 -u) |
  sort -n | awk '$2 != $3 {print $1; exit}')"
echo "first different frame (every frame compared): ${first:-none}"

if ((contra)); then
  [[ $rc -ne 0 && "$first" == "$EXPECTED" ]] || {
    echo "FAIL: counter-test expected the divergence at $EXPECTED, got ${first:-none} (rc $rc)" >&2
    exit 1; }
  echo "ok: without soft DAZ PC and PSP diverge at frame $EXPECTED, as expected"
  exit 0
fi
[[ $rc -eq 0 && -z "$first" ]] || { echo "FAIL: double PC and PSP differ (frame ${first:-?})" >&2; exit 1; }
echo "ok: double build equal to PC in every frame 1..2600 with the chapter-1 presses"
