#!/usr/bin/env bash
# Compares, pixel by pixel, the screen the PSP EBOOT shows at one guest frame with the screen the
# PC launcher shows at a guest frame (the same one by default), with the same scripted presses.
#   scripts/comparar-titulo.sh <EBOOT.PBP> <dir-juego-extraido> <ikaruga_boot> <ikaruga.toml>
#                              [--cuadro N] [--cuadro-pc M] [--sin-texturas] [--umbral DB]
#                              [--salida DIR] [--reusar] [--segundos S] [--pulsar P]
#                              [--cfg LINE]...
# --pulsar P replaces the scripted presses on both sides; --cfg LINE adds LINE to the PSP's
# ikaruga.cfg (repeatable).
# PSP: PPSSPP runs the EBOOT with ikaruga.cfg = capturar_en=N, pulsar=start@700, rtc_seed=1 (and
# texturas=0 with --sin-texturas); the EBOOT writes captura-N.raw (480x272 RGB888) and quits.
# PC: ikaruga_boot --window --rtc-seed 1 --press start@700 --screenshot-at M (M defaults to N).
# The PC screenshot is scaled from 640x480 to 480x272 exactly as the PSP scales vertices
# (x * 480/640, y * 272/480, whole screen, no letterbox), with a box filter.
# Prints PSNR over the RGB channels and the mean absolute error per channel. Exit 0 when
# PSNR >= DB (default 25), 1 otherwise, 2 on usage errors.
# start@700 is the one press between power-on and the title: without it the game stays on its
# boot screen. Default N: 1243, the first frame of the title that is identical to the settled
# title on PC (frames 1210-1242 fade in).
# Default DIR: build/psp/evidencia/titulo-N[-pc-M][-sin-texturas]. --reusar keeps captures in DIR.
# Env: PROBAR_DISCO as in scripts/probar-ikaruga-ppsspp.sh. Host needs: what comparar-geometria.sh
# needs.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESSES="start@700"
cuadro=1243 cuadro_pc="" sin_texturas=0 umbral=25 salida="" reusar=0 segundos=""
pos=() extra_cfg=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadro) cuadro="$2"; shift 2 ;;
    --cuadro-pc) cuadro_pc="$2"; shift 2 ;;
    --sin-texturas) sin_texturas=1; shift ;;
    --umbral) umbral="$2"; shift 2 ;;
    --salida) salida="$2"; shift 2 ;;
    --reusar) reusar=1; shift ;;
    --segundos) segundos="$2"; shift 2 ;;
    --pulsar) PRESSES="$2"; shift 2 ;;
    --cfg) extra_cfg+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 4 ]] || { sed -n '3,7p' "$0" >&2; exit 2; }
EBOOT="$(realpath "${pos[0]}")" JUEGO="$(realpath "${pos[1]}")"
BOOT="$(realpath "${pos[2]}")" TOML="$(realpath "${pos[3]}")"
cuadro_pc="${cuadro_pc:-$cuadro}"
[[ "$cuadro" =~ ^[0-9]+$ && "$cuadro_pc" =~ ^[0-9]+$ ]] || { echo "frames must be numbers" >&2; exit 2; }
sufijo=""
[[ "$cuadro_pc" != "$cuadro" ]] && sufijo="$sufijo-pc-$cuadro_pc"
[[ $sin_texturas -eq 1 ]] && sufijo="$sufijo-sin-texturas"
OUT="$(realpath -m "${salida:-$REPO/build/psp/evidencia/titulo-$cuadro$sufijo}")"
[[ -n "$segundos" ]] || segundos=$(( cuadro * 2 + 600 ))
mkdir -p "$OUT"

# PSP.
PSP_RAW="$OUT/captura-$cuadro.raw" PSP_LOG="$OUT/psp.log"
if [[ $reusar -eq 0 || ! -s "$PSP_RAW" ]]; then
  rm -f "$PSP_RAW"
  {
    echo "capturar_en=$cuadro"
    echo "pulsar=$PRESSES"
    echo "rtc_seed=1"
    [[ $sin_texturas -eq 1 ]] && echo "texturas=0"
    for l in "${extra_cfg[@]}"; do echo "$l"; done
  } > "$OUT/ikaruga.cfg"
  start=$(date +%s)
  PROBAR_CFG="$OUT/ikaruga.cfg" "$REPO/scripts/probar-ikaruga-ppsspp.sh" "$EBOOT" "$JUEGO" \
    "$segundos" "$PSP_LOG" > "$OUT/probar.out"
  echo "psp: PPSSPP wall time $(( $(date +%s) - start )) s" | tee "$OUT/psp-tiempo.txt"
fi
[[ -s "$PSP_RAW" ]] || { echo "comparar-titulo: no PSP capture (see $PSP_LOG)" >&2; exit 1; }

# PC.
PC_PPM="$OUT/pc-$cuadro_pc.ppm"
if [[ $reusar -eq 0 || ! -s "$PC_PPM" ]]; then
  work="$(mktemp -d "$OUT/.pc-XXXXXX")"
  ( cd "$work"
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
      timeout 600 "$BOOT" --config "$TOML" --window --rtc-seed 1 --no-audio \
      --press "$PRESSES" --screenshot-at "$cuadro_pc" --max-frames $(( cuadro_pc + 10 )) \
      > "$OUT/pc-window.log" 2>&1
    mv screenshot-000.ppm "$PC_PPM" )
  rm -rf "$work"
fi
[[ -s "$PC_PPM" ]] || { echo "comparar-titulo: no PC screenshot" >&2; exit 1; }

python3 - "$PSP_RAW" "$PC_PPM" "$OUT" "$cuadro" "$cuadro_pc" "$umbral" <<'PY' | tee "$OUT/resultado.txt"
import math, sys
from PIL import Image

raw, ppm, out, n, m, threshold = sys.argv[1:]
threshold = float(threshold)
psp = Image.frombytes("RGB", (480, 272), open(raw, "rb").read())
pc = Image.open(ppm).convert("RGB").resize((480, 272), Image.BOX)
psp.save(f"{out}/psp-{n}.png")
pc.save(f"{out}/pc-{m}-480x272.png")
a, b = psp.tobytes(), pc.tobytes()
se = [0, 0, 0]
ae = [0, 0, 0]
for i in range(len(a)):
    d = a[i] - b[i]
    se[i % 3] += d * d
    ae[i % 3] += abs(d)
pixels = 480 * 272
mse = sum(se) / (3 * pixels)
psnr = float("inf") if mse == 0 else 10 * math.log10(255 * 255 / mse)
print(f"psp frame {n} vs pc frame {m}")
print(f"mean abs error r {ae[0]/pixels:.2f} g {ae[1]/pixels:.2f} b {ae[2]/pixels:.2f}")
print(f"mse {mse:.2f} psnr {psnr:.2f} dB (threshold {threshold:g})")
ok = psnr >= threshold
print("PASS" if ok else f"FAIL: psnr {psnr:.2f} < {threshold:g}")
sys.exit(0 if ok else 1)
PY
