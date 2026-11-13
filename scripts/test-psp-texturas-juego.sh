#!/usr/bin/env bash
# Acceptance test of the texture cache in chapter 1 (docs/velocidad-psp.md), at guest frame N
# (default 2350) with the presses of scripts/test-psp-geometria.sh:
#   (a) the PSP's texture cache refuses no request in the captured render: the log line
#       "textures: textured ..." has rejected_this_render 0;
#   (b) PSNR against Flycast's image aligned with frame N (scripts/flycast-alineado.sh,
#       docs/referencia-flycast.md) is >= 20 dB;
#   (c) negative: with the old cache cap (ikaruga.cfg texturas_kb=2048, --presupuesto-viejo KB to
#       change it) the same render does refuse requests, so (a) can fail.
#   scripts/test-psp-texturas-juego.sh <EBOOT.PBP> <dir-juego-extraido>
#                                      [--cuadro N] [--presupuesto-viejo KB] [--reusar] [--sin-contraprueba]
# Runs scripts/comparar-titulo.sh twice (one PPSSPP at a time).
# Exit 0 when all checks pass. Env: PROBAR_DISCO (real disc HD area) as in
# scripts/probar-ikaruga-ppsspp.sh, FLYCAST_BIOS.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMP="$REPO/scripts/comparar-titulo.sh"
. "$REPO/scripts/escenas-flycast.sh"
cuadro="$CUADRO_CAP1" viejo=2048 reusar=() contra=1 args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --presupuesto-viejo) viejo="$2"; shift 2 ;;
    --reusar) reusar=(--reusar); shift ;;
    --sin-contraprueba) contra=0; shift ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 2 ]] || { sed -n '10,11p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
A="$EV/texturas-juego-$cuadro" C="$EV/texturas-juego-$cuadro-presupuesto-$viejo"
mkdir -p "$EV"

psnr() { sed -n 's/^  todo *psnr *\([0-9.inf]*\) dB.*/\1/p' "$1/resultado.txt" 2>/dev/null | head -1 || true; }
rechazos() {  # over_budget (whole run) + rejected_this_render of the last "textures: textured" line
  grep -a 'textures: textured' "$1/psp.log" 2>/dev/null | tail -1 |
    sed -n 's/.* over_budget \([0-9]*\) .* rejected_this_render \([0-9]*\).*/\1 \2/p' |
    awk '{print $1 + $2}'
}

fallos=0
# dibujar_desde=0: (a) counts over_budget over the whole run, so the cache must see every render
# (comparar-titulo.sh would otherwise skip the video until 10 frames before the capture).
set +e
"$CMP" "${args[@]}" --cuadro "$cuadro" --pulsar "$PRESSES_CAP1" --alineado \
  --salida "$A" --exigir 'todo:psnr>=20' --cfg dibujar_desde=0 "${reusar[@]}" > "$EV/test-texturas-juego-a.out" 2>&1
set -e
ra="$(rechazos "$A")" pa="$(psnr "$A")"
if [[ "$ra" == 0 ]]; then
  echo "PASS (a) frame $cuadro: over_budget + rejected_this_render 0"
else
  echo "FAIL (a) frame $cuadro: rejected_this_render ${ra:-missing} (see $A/psp.log)"; fallos=1
fi
if [[ -z "$pa" ]]; then
  echo "FAIL (b) no PSNR (see $EV/test-texturas-juego-a.out)"; fallos=1
elif python3 -c "import sys; sys.exit(0 if float('$pa') >= 20 else 1)"; then
  echo "PASS (b) psnr $pa dB >= 20 against Flycast"
else
  echo "FAIL (b) psnr $pa dB < 20 against Flycast (see $A/resultado.txt)"; fallos=1
fi

if [[ $contra -eq 1 ]]; then
  mkdir -p "$C"
  set +e
  "$CMP" "${args[@]}" --cuadro "$cuadro" --pulsar "$PRESSES_CAP1" --alineado --salida "$C" \
    --cfg dibujar_desde=0 --cfg "texturas_kb=$viejo" "${reusar[@]}" > "$EV/test-texturas-juego-c.out" 2>&1
  set -e
  rc="$(rechazos "$C")" pc="$(psnr "$C")"
  if [[ -n "$rc" && "$rc" -gt 0 ]]; then
    echo "PASS (c) texturas_kb=$viejo: over_budget + rejected_this_render $rc > 0 (psnr ${pc:-?} dB)"
  else
    echo "FAIL (c) texturas_kb=$viejo: over_budget + rejected_this_render ${rc:-missing}, expected > 0"; fallos=1
  fi
fi
exit $fallos
