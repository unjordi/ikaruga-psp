#!/usr/bin/env bash
# Acceptance tests of rows 6b and 6c (docs/video-psp.md), with the presses of
# scripts/test-psp-geometria.sh, through scripts/comparar-titulo.sh and a PSNR per region:
#   (a) frame 2600 (chapter 1): the side columns (x < 100 and x >= 380 at 480x272, the HUD) are
#       >= 25 dB against PC; with the user tile clip off (ikaruga.cfg recorte=0) they are < 15 dB;
#   (b) frame 1700 (SELECT GAME MODE): the mode buttons (60,105)-(270,225) are >= 20 dB against
#       PC; with fog off (niebla=0) they are < 20 dB;
#   (c) the title (scripts/test-psp-titulo.sh's (a), frame 1243, start@700) is still >= 25 dB.
#   scripts/test-psp-video.sh <EBOOT.PBP> <dir-juego-extraido> <ikaruga_boot> <ikaruga.toml>
#                             [--reusar] [--solo a|b|c]
# Five PPSSPP runs, one at a time (~45 min); the negative runs reuse the PC screenshots.
# Env: PROBAR_DISCO (real disc HD area) as in scripts/probar-ikaruga-ppsspp.sh.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMP="$REPO/scripts/comparar-titulo.sh"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
args=() reusar=() solo=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --reusar) reusar=(--reusar); shift ;;
    --solo) solo="$2"; shift 2 ;;
    -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 4 ]] || { sed -n '9,10p' "$0" >&2; exit 2; }
EV="$REPO/build/psp/evidencia"
mkdir -p "$EV"

# region_psnr DIR N BOX... : PSNR over the union of boxes x0,y0,x1,y1 (480x272, right/bottom
# exclusive) between psp-N.png and pc-N-480x272.png in DIR.
region_psnr() {
  python3 - "$@" <<'PY'
import math, sys
from PIL import Image
d, n, *boxes = sys.argv[1:]
a = Image.open(f"{d}/psp-{n}.png").convert("RGB")
b = Image.open(f"{d}/pc-{n}-480x272.png").convert("RGB")
se = cnt = 0
for box in boxes:
    box = tuple(int(v) for v in box.split(","))
    A, B = a.crop(box).tobytes(), b.crop(box).tobytes()
    se += sum((x - y) ** 2 for x, y in zip(A, B))
    cnt += len(A)
mse = se / cnt
print(f"{99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse):.2f}")
PY
}
# run DIR N SHARE [cfg...]: one capture at frame N; PC screenshot copied from SHARE when present.
run() {
  local dir="$1" n="$2" share="$3"; shift 3
  local cfg=()
  for c in "$@"; do cfg+=(--cfg "$c"); done
  mkdir -p "$dir"
  [[ -n "$share" && -s "$share/pc-$n.ppm" && ! -s "$dir/pc-$n.ppm" ]] && cp "$share/pc-$n.ppm" "$dir/"
  "$CMP" "${args[@]}" --cuadro "$n" --pulsar "$PRESSES" --umbral 0 --salida "$dir" \
    --reusar "${cfg[@]}" > "$dir.out" 2>&1 || true
}
ge() { python3 -c "import sys; sys.exit(0 if float('$1') >= float('$2') else 1)"; }

fail=0
if [[ -z "$solo" || "$solo" == a ]]; then
  A="$EV/video-2600" A0="$EV/video-2600-recorte-0"
  [[ ${#reusar[@]} -eq 0 ]] && rm -f "$A/captura-2600.raw" "$A0/captura-2600.raw"
  run "$A" 2600 ""
  run "$A0" 2600 "$A" recorte=0
  la="$(region_psnr "$A" 2600 0,0,100,272 380,0,480,272 2>/dev/null || echo 0)"
  l0="$(region_psnr "$A0" 2600 0,0,100,272 380,0,480,272 2>/dev/null || echo 99)"
  if ge "$la" 25 && ! ge "$l0" 15; then
    echo "(a) ok: side columns $la dB >= 25; with recorte=0 $l0 dB < 15"
  else
    echo "(a) FAIL: side columns $la dB (>= 25?), recorte=0 $l0 dB (< 15?)"; fail=1
  fi
fi
if [[ -z "$solo" || "$solo" == b ]]; then
  B="$EV/video-1700" B0="$EV/video-1700-niebla-0"
  [[ ${#reusar[@]} -eq 0 ]] && rm -f "$B/captura-1700.raw" "$B0/captura-1700.raw"
  run "$B" 1700 ""
  run "$B0" 1700 "$B" niebla=0
  ba="$(region_psnr "$B" 1700 60,105,270,225 2>/dev/null || echo 0)"
  b0="$(region_psnr "$B0" 1700 60,105,270,225 2>/dev/null || echo 99)"
  if ge "$ba" 20 && ! ge "$b0" 20; then
    echo "(b) ok: mode buttons $ba dB >= 20; with niebla=0 $b0 dB < 20"
  else
    echo "(b) FAIL: mode buttons $ba dB (>= 20?), niebla=0 $b0 dB (< 20?)"; fail=1
  fi
fi
if [[ -z "$solo" || "$solo" == c ]]; then
  C="$EV/video-titulo-1243"
  if "$CMP" "${args[@]}" --salida "$C" "${reusar[@]}" > "$C.out" 2>&1; then
    echo "(c) ok: title $(sed -n 's/.* psnr \([0-9.inf]*\) dB.*/\1/p' "$C/resultado.txt") dB >= 25"
  else
    echo "(c) FAIL: title (see $C/resultado.txt)"; fail=1
  fi
fi
exit $fail
