#!/usr/bin/env bash
# Acceptance tests of the textured title screen (docs/titulo-psp.md), all through
# scripts/comparar-titulo.sh at guest frame N (default 1243):
#   (a) PSNR(PSP, PC) >= 25 dB at frame N;
#   (b) negative: with textures off (ikaruga.cfg texturas=0) the same comparison fails, below 25 dB
#       and at least 6 dB below (a);
#   (c) discrimination: the PSP capture of frame N against PC's frame of another scene (the
#       company logo, frame 800 by default) is below 25 dB;
#   (d) with --todas, the other eight PSP tests still pass (the renderer only reads guest state).
#   scripts/test-psp-titulo.sh <EBOOT.PBP> <dir-juego-extraido> <ikaruga_boot> <ikaruga.toml>
#                              [--cuadro N] [--cuadro-otra M] [--reusar] [--todas]
# PPSSPP runs one at a time (two under xvfb-run share a display and fail): (a) and (b) are
# sequential, (c) reuses (a)'s PSP capture.
# Env: PROBAR_DISCO as in scripts/probar-ikaruga-ppsspp.sh; for (d), what each test needs
# (DREAM_RECOMP for test-psp-igual-pc.sh).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMP="$REPO/scripts/comparar-titulo.sh"
args=() reusar=() cuadro=1243 otra=800 todas=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --cuadro-otra) otra="$2"; shift 2 ;;
    --reusar) reusar=(--reusar); shift ;;
    --todas) todas=1; shift ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 4 ]] || { sed -n '11,12p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
A="$EV/titulo-$cuadro" B="$EV/titulo-$cuadro-sin-texturas" C="$EV/titulo-$cuadro-pc-$otra"
mkdir -p "$EV"

psnr() { sed -n 's/.* psnr \([0-9.inf]*\) dB.*/\1/p' "$1/resultado.txt" 2>/dev/null || true; }

set +e
"$CMP" "${args[@]}" --cuadro "$cuadro" --salida "$A" "${reusar[@]}" > "$EV/test-titulo-a.out" 2>&1
rc_a=$?
"$CMP" "${args[@]}" --cuadro "$cuadro" --sin-texturas --salida "$B" "${reusar[@]}" \
  > "$EV/test-titulo-b.out" 2>&1
rc_b=$?
mkdir -p "$C"
[[ -s "$A/captura-$cuadro.raw" ]] && cp "$A/captura-$cuadro.raw" "$C/"
"$CMP" "${args[@]}" --cuadro "$cuadro" --cuadro-pc "$otra" --salida "$C" --reusar \
  > "$EV/test-titulo-c.out" 2>&1
rc_c=$?
set -e

pa="$(psnr "$A")" pb="$(psnr "$B")" pc="$(psnr "$C")"
fail=0
if [[ $rc_a -eq 0 && -n "$pa" ]]; then
  echo "(a) ok: psnr $pa dB >= 25 at frame $cuadro"
else
  echo "(a) FAIL: psnr ${pa:-none} (see $A/resultado.txt)"; fail=1
fi
if [[ $rc_b -ne 0 && -n "$pb" && -n "$pa" ]] &&
   python3 -c "import sys; a,b=float('$pa'),float('$pb'); sys.exit(0 if b < 25 and a - b >= 6 else 1)"; then
  echo "(b) ok: without textures psnr $pb dB, < 25 and $(python3 -c "print(round($pa-$pb,2))") dB below (a)"
else
  echo "(b) FAIL: without textures psnr ${pb:-none} dB (see $B/resultado.txt)"; fail=1
fi
if [[ $rc_c -ne 0 && -n "$pc" ]]; then
  echo "(c) ok: against PC frame $otra psnr $pc dB < 25"
else
  echo "(c) FAIL: against PC frame $otra psnr ${pc:-none} dB (see $C/resultado.txt)"; fail=1
fi

if [[ $todas -eq 1 ]]; then
  E="${args[0]}" G="${args[1]}" BOOT="${args[2]}" TOML="${args[3]}"
  run() {
    local name="$1"; shift
    if "$@" > "$EV/test-titulo-d-$name.out" 2>&1; then echo "(d) ok: $name"
    else echo "(d) FAIL: $name (see $EV/test-titulo-d-$name.out)"; fail=1; fi
  }
  run arranque "$REPO/scripts/test-psp-arranque.sh" "$E" "$G"
  # DREAM_RECOMP may point at an already patched tree; this test patches the submodule copy.
  run redondeo env -u DREAM_RECOMP "$REPO/scripts/test-psp-redondeo.sh"
  run rtc "$REPO/scripts/test-psp-rtc.sh" "$E" "$G"
  run carpeta "$REPO/scripts/test-psp-carpeta.sh" "$E" "$G"
  run vmu "$REPO/scripts/test-psp-vmu.sh" "$E" "$G"
  run salida-suspension "$REPO/scripts/test-psp-salida-suspension.sh" "$E" "$G"
  run igual-pc "$REPO/scripts/test-psp-igual-pc.sh" "$E" "$G"
  run geometria "$REPO/scripts/test-psp-geometria.sh" "$E" "$G" "$BOOT" "$TOML"
fi
exit $fail
