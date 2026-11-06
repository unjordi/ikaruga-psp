#!/usr/bin/env bash
# Compares the geometry the PSP EBOOT draws at one guest frame with what the PC launcher draws at
# the same frame, with the same scripted presses.
#   scripts/comparar-geometria.sh <EBOOT.PBP> <dir-juego-extraido> <ikaruga_boot> <ikaruga.toml>
#                                 [--cuadro N] [--sin-dibujo] [--salida DIR] [--reusar]
#                                 [--segundos S]
# PSP: PPSSPP runs the EBOOT with ms0:/PSP/GAME/IKARUGA/ikaruga.cfg = capturar_en=N, pulsar=<P>
# and rtc_seed=1 (and dibujar=0 with --sin-dibujo); the EBOOT writes captura-N.raw (480x272 RGB888) and quits.
# PC: ikaruga_boot --window --rtc-seed 1 --press <P> --screenshot-at N, scaled to 480x272; and a
# headless run with --dump-ta-at-frame N whose stream is decoded by contar-poligonos.
# Each image: background = the clear colour both renderers use (black); mask = pixels with any
# channel above 24. Untextured, the PSP draws textured polygons in their vertex colour (often
# white), so the most common colour of its capture can be geometry, not background. Passes (exit 0) when the PSP mask covers > 20 % of the screen, IoU(PSP, PC) >= 0.6 and
# the polygon counts are equal; exit 1 otherwise.
# --reusar keeps an existing PSP capture/log and PC screenshot/stream in DIR.
# Default DIR: build/psp/evidencia/geometria-N[-sin-dibujo]. Default N: 2600.
# Host needs: what probar-ikaruga-ppsspp.sh needs, g++, python3 with Pillow, a Wayland session for
# the PC window (WAYLAND_DISPLAY/XDG_RUNTIME_DIR; defaults wayland-1 and /run/user/<uid>).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
cuadro=2600 sin_dibujo=0 salida="" reusar=0 segundos=""
pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --sin-dibujo) sin_dibujo=1; shift ;;
    --salida) salida="$2"; shift 2 ;;
    --reusar) reusar=1; shift ;;
    --segundos) segundos="$2"; shift 2 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 4 ]] || { sed -n '3,5p' "$0" >&2; exit 2; }
EBOOT="$(realpath "${pos[0]}")" JUEGO="$(realpath "${pos[1]}")"
BOOT="$(realpath "${pos[2]}")" TOML="$(realpath "${pos[3]}")"
[[ "$cuadro" =~ ^[0-9]+$ ]] || { echo "--cuadro must be a number" >&2; exit 2; }
sufijo=""; [[ $sin_dibujo -eq 1 ]] && sufijo="-sin-dibujo"
OUT="$(realpath -m "${salida:-$REPO/build/psp/evidencia/geometria-$cuadro$sufijo}")"
# PPSSPP reaches ~0.03x real time: frame N needs about N/60/0.03 s, plus margin.
[[ -n "$segundos" ]] || segundos=$(( cuadro * 2 + 600 ))
mkdir -p "$OUT"

# Polygon counter for the PC stream: the submodule's decoder, built for the host.
CONTAR="$REPO/build/host/contar-poligonos"
DR="$REPO/third_party/dream-recomp/render"
if [[ ! -x "$CONTAR" || "$REPO/scripts/contar-poligonos.cpp" -nt "$CONTAR" ]]; then
  mkdir -p "$(dirname "$CONTAR")"
  g++ -std=c++20 -O2 -I"$DR/include" "$REPO/scripts/contar-poligonos.cpp" \
    "$DR/src/display_list.cpp" -o "$CONTAR"
fi

# PSP.
PSP_RAW="$OUT/captura-$cuadro.raw" PSP_LOG="$OUT/psp.log"
if [[ $reusar -eq 0 || ! -s "$PSP_RAW" ]]; then
  rm -f "$PSP_RAW"
  {
    echo "capturar_en=$cuadro"
    echo "pulsar=$PRESSES"
    echo "rtc_seed=1"
    [[ $sin_dibujo -eq 1 ]] && echo "dibujar=0"
  } > "$OUT/ikaruga.cfg"
  start=$(date +%s)
  PROBAR_CFG="$OUT/ikaruga.cfg" "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" \
    "$segundos" "$PSP_LOG" >/dev/null
  echo "psp: PPSSPP wall time $(( $(date +%s) - start )) s" | tee "$OUT/psp-tiempo.txt"
fi
[[ -s "$PSP_RAW" ]] || { echo "comparar-geometria: no PSP capture (see $PSP_LOG)" >&2; exit 1; }
python3 - "$PSP_RAW" "$OUT/psp-$cuadro.png" <<'PY'
import sys
from PIL import Image
Image.frombytes("RGB", (480, 272), open(sys.argv[1], "rb").read()).save(sys.argv[2])
PY

# PC.
PC_PPM="$OUT/pc-$cuadro.ppm" PC_TA="$OUT/pc-ta-$cuadro.bin"
if [[ $reusar -eq 0 || ! -s "$PC_PPM" || ! -s "$PC_TA" ]]; then
  work="$(mktemp -d "$OUT/.pc-XXXXXX")"
  ( cd "$work"
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
      timeout 600 "$BOOT" --config "$TOML" --window --rtc-seed 1 --no-audio \
      --press "$PRESSES" --screenshot-at "$cuadro" --max-frames $(( cuadro + 10 )) > "$OUT/pc-window.log" 2>&1
    mv screenshot-000.ppm "$PC_PPM"
    timeout 600 "$BOOT" --config "$TOML" --rtc-seed 1 --no-audio --press "$PRESSES" \
      --dump-ta "$PC_TA" --dump-ta-at-frame "$cuadro" --max-frames $(( cuadro + 10 )) \
      > "$OUT/pc-dump.log" 2>&1 )
  rm -rf "$work"
fi
[[ -s "$PC_PPM" && -s "$PC_TA" ]] || { echo "comparar-geometria: no PC screenshot or stream" >&2; exit 1; }

# Counts on both sides.
psp_line="$(grep -m1 '^render: ' "$PSP_LOG" || true)"
psp_polys="$(sed -n 's/.* polygons \([0-9]*\) .*/\1/p' <<<"$psp_line")"
pc_line="$("$CONTAR" "$PC_TA")"
pc_polys="$(sed -n 's/.* polygons \([0-9]*\) .*/\1/p' <<<"$pc_line")"
pc_at="$(grep -m1 '^wrote ' "$OUT/pc-dump.log" | sed -n 's/.*\(render [0-9]* at frame [0-9]*\).*/\1/p')"
{
  echo "psp: $psp_line"
  echo "pc:  $pc_at $pc_line"
} | tee "$OUT/conteos.txt"

python3 - "$OUT/psp-$cuadro.png" "$PC_PPM" "$OUT" "$cuadro" "${psp_polys:-x}" "${pc_polys:-y}" \
  <<'PY' | tee "$OUT/resultado.txt"
import sys
from collections import Counter
from PIL import Image

psp_png, pc_ppm, out, frame, psp_polys, pc_polys = sys.argv[1:]
THRESHOLD = 24

BACKGROUND = (0, 0, 0)

def mask(im):
    px = list(im.convert("RGB").getdata())
    m = [max(abs(a - b) for a, b in zip(p, BACKGROUND)) > THRESHOLD for p in px]
    return Counter(px).most_common(1)[0][0], m

psp = Image.open(psp_png).convert("RGB")
pc = Image.open(pc_ppm).convert("RGB").resize((480, 272), Image.BILINEAR)
pc.save(f"{out}/pc-{frame}-480x272.png")
bg_psp, m_psp = mask(psp)
bg_pc, m_pc = mask(pc)
n = len(m_psp)
inter = sum(a and b for a, b in zip(m_psp, m_pc))
union = sum(a or b for a, b in zip(m_psp, m_pc))
iou = inter / union if union else 0.0
cover_psp = sum(m_psp) / n
cover_pc = sum(m_pc) / n
colours = len(set(psp.getdata()))
for name, m in (("psp", m_psp), ("pc", m_pc)):
    Image.frombytes("L", (480, 272), bytes(255 if v else 0 for v in m)).save(f"{out}/mascara-{name}-{frame}.png")
print(f"frame {frame}")
print(f"background {BACKGROUND}, threshold {THRESHOLD}")
print(f"psp: most common {bg_psp} geometry {cover_psp:.4f} of the screen, {colours} colours")
print(f"pc:  most common {bg_pc} geometry {cover_pc:.4f} of the screen")
print(f"iou {iou:.4f} (intersection {inter}, union {union})")
print(f"pc geometry also in psp {inter / max(1, sum(m_pc)):.4f}; psp geometry also in pc {inter / max(1, sum(m_psp)):.4f}")
print(f"polygons psp {psp_polys} pc {pc_polys}")
fails = []
if cover_psp <= 0.20:
    fails.append(f"PSP geometry covers {cover_psp:.4f} <= 0.20")
if iou < 0.6:
    fails.append(f"IoU {iou:.4f} < 0.6")
if psp_polys != pc_polys:
    fails.append(f"polygon counts differ ({psp_polys} vs {pc_polys})")
print("PASS" if not fails else "FAIL: " + "; ".join(fails))
sys.exit(1 if fails else 0)
PY
