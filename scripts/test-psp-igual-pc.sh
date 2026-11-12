#!/usr/bin/env bash
# PC <-> PSP oracle: the PSP runs the same game as PC, store for store. Runs the PC launcher with
# --write-hash and the EBOOT in PPSSPP (no input, RTC seed 1 on both) and compares the rolling hash
# of guest stores (DcMemory::write_hash) at frames 60, 120, ... 600. Fails at the first frame that
# differs, or that the PSP did not reach, naming it.
#   scripts/test-psp-igual-pc.sh <EBOOT.PBP> <dir-juego-extraido> [segundos (default 330)]
# Env: DREAM_RECOMP=<dream-recomp checkout with build-rel/games/ikaruga/ikaruga_boot>
#      (default third_party/dream-recomp); HASTA=<last frame> (default 600);
#      RTC_SEED=<n> (default 1; the PSP gets it through ikaruga.cfg);
#      PROBAR_CFG_EXTRA=<file> more ikaruga.cfg lines for the PSP run (debug keys);
#      IGUAL_OUT=<dir> instead of build/psp/evidencia/igual-pc;
#      PULSAR=<presses> same scripted input on both (PC --press, PSP pulsar=), e.g. the chapter-1
#      presses of medir-velocidad.sh; without input FTRV/FIPR results never reach memory before
#      frame 600, so only PULSAR + HASTA>=1500 tells float from double (docs/velocidad-psp.md).
# Logs: build/psp/evidencia/igual-pc/{pc-hash.txt,psp.log}.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 2 && $# -le 3 ]] || { sed -n '6p' "$0" >&2; exit 2; }
EBOOT="$(realpath "$1")" JUEGO="$(realpath "$2")" SEGUNDOS="${3:-330}"
DR="$(realpath "${DREAM_RECOMP:-$REPO/third_party/dream-recomp}")"
HASTA="${HASTA:-600}" SEED="${RTC_SEED:-1}" PULSAR="${PULSAR:-}"
pc_press=()
[[ -n "$PULSAR" ]] && pc_press=(--press "$PULSAR")
BOOT="$DR/build-rel/games/ikaruga/ikaruga_boot"
CFG="$DR/games/ikaruga/ikaruga.toml"
OUT="${IGUAL_OUT:-$REPO/build/psp/evidencia/igual-pc}"
[[ -x "$BOOT" ]] || { echo "no PC launcher at $BOOT (see docs/reproducir-pc.md)" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"

echo "== PC: $HASTA frames, --rtc-seed $SEED"
(cd "$OUT" && "$BOOT" --config "$CFG" --vmu pc.vmu --max-frames "$HASTA" --rtc-seed "$SEED" \
  "${pc_press[@]}" --no-audio --write-hash pc-hash.txt >pc.out 2>&1) || true
[[ -s "$OUT/pc-hash.txt" ]] || { echo "FAIL: the PC run wrote no hashes ($OUT/pc.out)" >&2; exit 1; }

echo "== PSP: $SEGUNDOS s in PPSSPP, rtc_semilla = $SEED"
printf 'rtc_semilla = %s\n' "$SEED" >"$OUT/ikaruga.cfg"
[[ -n "$PULSAR" ]] && printf 'pulsar=%s\n' "$PULSAR" >>"$OUT/ikaruga.cfg"
# The EBOOT stops one frame after the last hash compared, so the run lasts as long as the
# emulation needs instead of the whole $SEGUNDOS (an upper bound now).
printf 'max_cuadros=%s\n' "$((HASTA + 1))" >>"$OUT/ikaruga.cfg"
[[ -n "${PROBAR_CFG_EXTRA:-}" ]] && grep -v '^rtc_semilla' "$PROBAR_CFG_EXTRA" >>"$OUT/ikaruga.cfg"
PROBAR_SIN_LIMITE="${PROBAR_SIN_LIMITE:-1}" PROBAR_CFG="$OUT/ikaruga.cfg" \
  "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" \
  "$SEGUNDOS" "$OUT/psp.log" >/dev/null 2>"$OUT/probar.err" || true
[[ -s "$OUT/psp.log" ]] || { echo "FAIL: the EBOOT wrote no log" >&2; exit 1; }

iguales=0
for ((f = 60; f <= HASTA; f += 60)); do
  pc="$(awk -v f="$f" '$1 == f { print $3; exit }' "$OUT/pc-hash.txt")"
  psp="$(awk -v f="$f" '$1 == "hash" && $2 == f { print $4; exit }' "$OUT/psp.log")"
  [[ -n "$pc" ]] || { echo "FAIL: frame $f: the PC run has no hash" >&2; exit 1; }
  if [[ -z "$psp" ]]; then
    echo "FAIL: frame $f: the PSP did not reach it in $SEGUNDOS s ($iguales frames equal before)" >&2
    exit 1
  fi
  if [[ "$pc" != "$psp" ]]; then
    echo "FAIL: first different frame $f: PC $pc PSP $psp ($iguales frames equal before)" >&2
    exit 1
  fi
  echo "frame $f: $pc"
  iguales=$((iguales + 1))
done
echo "ok: PC and PSP write_hash equal at $iguales frames (60..$HASTA)"
