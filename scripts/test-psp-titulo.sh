#!/usr/bin/env bash
# Acceptance tests of the textured title screen (docs/titulo-psp.md), against the Flycast
# reference series (docs/referencia-flycast.md), at guest frame N (default 1243, start@700):
#   (a) whole screen PSNR >= 25 dB and SSIM >= 0.85 against the title window of the series;
#   (b) negative: with textures off (ikaruga.cfg texturas=0) the same comparison is below 25 dB;
#   (c) discrimination: the PSP capture of (a) against the SELECT GAME MODE window is below 25 dB;
#   (d) with --todas, the other PSP tests still pass (the renderer only reads guest state).
#   scripts/test-psp-titulo.sh <EBOOT.PBP> <dir-juego-extraido> [--cuadro N] [--reusar] [--todas]
# PPSSPP runs one at a time: (a) and (b) are sequential, (c) reuses (a)'s PSP capture.
# Env: PROBAR_DISCO as in scripts/probar-ikaruga-ppsspp.sh; for (d), what each test needs
# (DREAM_RECOMP for test-psp-igual-pc.sh).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/scripts/escenas-flycast.sh"
CMP="$REPO/scripts/comparar-titulo.sh"
FLY="$REPO/scripts/comparar-flycast.sh"
args=() reusar=() cuadro=1243 todas=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --reusar) reusar=(--reusar); shift ;;
    --todas) todas=1; shift ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 2 ]] || { sed -n '7p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
A="$EV/titulo-$cuadro" B="$EV/titulo-$cuadro-sin-texturas" C="$EV/titulo-$cuadro-select"
mkdir -p "$EV" "$C"

val() { sed -n "s/^  $2 *psnr *\([0-9.inf]*\) dB  ssim \([0-9.]*\)\$/\1 \2/p" "$1/resultado.txt" 2>/dev/null | head -1; }

set +e
"$CMP" "${args[@]}" --cuadro "$cuadro" --ventana "$VENTANA_TITULO" --salida "$A" "${reusar[@]}" \
  --exigir 'todo:psnr>=25' --exigir 'todo:ssim>=0.85' > "$EV/test-titulo-a.out" 2>&1
rc_a=$?
"$CMP" "${args[@]}" --cuadro "$cuadro" --ventana "$VENTANA_TITULO" --sin-texturas --salida "$B" "${reusar[@]}" \
  --sin-borde --exigir 'todo:psnr<25' > "$EV/test-titulo-b.out" 2>&1
rc_b=$?
"$FLY" --captura "$A/captura-$cuadro.raw" --ventana "$VENTANA_SELECT" --sin-borde --salida "$C" \
  --exigir 'todo:psnr<25' > "$EV/test-titulo-c.out" 2>&1
rc_c=$?
set -e

read -r pa sa <<<"$(val "$A" todo)"
read -r pb _ <<<"$(val "$B" todo)"
read -r pc _ <<<"$(val "$C" todo)"
fail=0
if [[ $rc_a -eq 0 ]]; then
  echo "(a) ok: psnr $pa dB >= 25, ssim $sa >= 0.85 at frame $cuadro"
else
  echo "(a) FAIL (exit $rc_a): psnr ${pa:-none} ssim ${sa:-none} (see $A/resultado.txt)"; fail=1
fi
if [[ $rc_b -eq 0 ]]; then
  echo "(b) ok: without textures psnr $pb dB < 25"
else
  echo "(b) FAIL (exit $rc_b): without textures psnr ${pb:-none} dB (see $B/resultado.txt)"; fail=1
fi
if [[ $rc_c -eq 0 ]]; then
  echo "(c) ok: against the SELECT GAME MODE window psnr $pc dB < 25"
else
  echo "(c) FAIL (exit $rc_c): against the SELECT GAME MODE window psnr ${pc:-none} dB (see $C/resultado.txt)"; fail=1
fi

if [[ $todas -eq 1 ]]; then
  E="${args[0]}" G="${args[1]}"
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
  run geometria "$REPO/scripts/test-psp-geometria.sh" "$E" "$G"
fi
exit $fail
