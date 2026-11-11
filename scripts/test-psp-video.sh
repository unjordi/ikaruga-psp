#!/usr/bin/env bash
# Acceptance tests of rows 6b and 6c (docs/video-psp.md), against the Flycast reference series
# (docs/referencia-flycast.md), with the presses of scripts/test-psp-geometria.sh, through
# scripts/comparar-titulo.sh:
#   (a) frame 2350 (start of chapter 1): the side columns (izquierda x < 100, derecha x >= 380 at 480x272,
#       the HUD) have SSIM >= 0.7 each against Flycast; with the user tile clip off (ikaruga.cfg
#       recorte=0) at least one is < 0.7;
#   (b) frame 1750 (SELECT GAME MODE): whole screen >= 20 dB and SSIM >= 0.7, and the mode buttons
#       (60,105)-(270,225) >= 20 dB; with fog off (niebla=0) the buttons are < 20 dB;
#   (c) the title (frame 1243, start@700) is >= 25 dB and SSIM >= 0.85 (the title test's (a)).
#   scripts/test-psp-video.sh <EBOOT.PBP> <dir-juego-extraido> [--reusar] [--solo a|b|c]
# Five PPSSPP runs, one at a time, unthrottled (about 20-40 s each).
# Env: PROBAR_DISCO (real disc HD area) as in scripts/probar-ikaruga-ppsspp.sh.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/scripts/escenas-flycast.sh"
CMP="$REPO/scripts/comparar-titulo.sh"
BOTONES="botones:60,105,270,225"
args=() reusar=() solo=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --reusar) reusar=(--reusar); shift ;;
    --solo) solo="$2"; shift 2 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 2 ]] || { sed -n '10p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
mkdir -p "$EV"

# run DIR N WINDOW EXIGIR [CFG...]: one capture at frame N compared with the window; returns
# comparar's code. A run with CFG is a negative control: no scene-edge check.
run() {
  local dir="$1" n="$2" win="$3" exigir="$4" cfg=() ex=() c r nb=()
  shift 4
  [[ $# -gt 0 ]] && nb=(--sin-borde)
  for c in "$@"; do cfg+=(--cfg "$c"); done
  for r in $exigir; do ex+=(--exigir "$r"); done
  "$CMP" "${args[@]}" --cuadro "$n" --pulsar "$PRESSES_CAP1" --ventana "$win" --salida "$dir" \
    "${reusar[@]}" --region "$BOTONES" "${nb[@]}" "${ex[@]}" "${cfg[@]}" > "$dir.out" 2>&1
}
# val DIR REGION METRIC: the number resultado.txt reports.
val() {
  local col=3; [[ "$3" == ssim ]] && col=6
  awk -v r="$2" -v c="$col" '$1 == r && $2 == "psnr" {print $c; exit}' "$1/resultado.txt" 2>/dev/null
}

fail=0
if [[ -z "$solo" || "$solo" == a ]]; then
  A="$EV/video-$CUADRO_CAP1" A0="$EV/video-$CUADRO_CAP1-recorte-0"
  set +e
  run "$A" "$CUADRO_CAP1" "$VENTANA_CAP1" 'izquierda:ssim>=0.7 derecha:ssim>=0.7'; rc=$?
  run "$A0" "$CUADRO_CAP1" "$VENTANA_CAP1" 'izquierda:ssim<0.7|derecha:ssim<0.7' recorte=0; rc0=$?
  set -e
  if [[ $rc -eq 0 && $rc0 -eq 0 ]]; then
    echo "(a) ok: side columns ssim $(val "$A" izquierda ssim)/$(val "$A" derecha ssim) >= 0.7; with recorte=0 $(val "$A0" izquierda ssim)/$(val "$A0" derecha ssim) (one < 0.7)"
  else
    echo "(a) FAIL: side columns ssim $(val "$A" izquierda ssim)/$(val "$A" derecha ssim) (exit $rc, both >= 0.7?); recorte=0 $(val "$A0" izquierda ssim)/$(val "$A0" derecha ssim) (exit $rc0, one < 0.7?) (see $A/resultado.txt)"; fail=1
  fi
fi
if [[ -z "$solo" || "$solo" == b ]]; then
  B="$EV/video-$CUADRO_SELECT" B0="$EV/video-$CUADRO_SELECT-niebla-0"
  set +e
  run "$B" "$CUADRO_SELECT" "$VENTANA_SELECT" 'todo:psnr>=20 todo:ssim>=0.7 botones:psnr>=20'; rc=$?
  run "$B0" "$CUADRO_SELECT" "$VENTANA_SELECT" 'botones:psnr<20' niebla=0; rc0=$?
  set -e
  if [[ $rc -eq 0 && $rc0 -eq 0 ]]; then
    echo "(b) ok: whole screen $(val "$B" todo psnr) dB / ssim $(val "$B" todo ssim), mode buttons $(val "$B" botones psnr) dB >= 20; with niebla=0 $(val "$B0" botones psnr) dB < 20"
  else
    echo "(b) FAIL: whole screen $(val "$B" todo psnr) dB / $(val "$B" todo ssim) (exit $rc), buttons $(val "$B" botones psnr) dB; niebla=0 buttons $(val "$B0" botones psnr) dB (exit $rc0, < 20?) (see $B/resultado.txt)"; fail=1
  fi
fi
if [[ -z "$solo" || "$solo" == c ]]; then
  C="$EV/video-titulo-1243"
  if "$CMP" "${args[@]}" --ventana "$VENTANA_TITULO" --salida "$C" "${reusar[@]}" \
       --exigir 'todo:psnr>=25' --exigir 'todo:ssim>=0.85' > "$C.out" 2>&1; then
    echo "(c) ok: title $(val "$C" todo psnr) dB / ssim $(val "$C" todo ssim)"
  else
    echo "(c) FAIL: title (see $C/resultado.txt)"; fail=1
  fi
fi
exit $fail
