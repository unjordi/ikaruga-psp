#!/usr/bin/env bash
# Runs Ikaruga in Flycast (flatpak org.flycast.Flycast, branch stable) on a virtual X display, with
# scripted presses, and records the screen as a series of PNGs (640x480 and scaled to 480x272).
#   scripts/capturar-flycast.sh <disco.chd> [--pulsar P] [--segundos S] [--fps F] [--salida DIR]
# How: Flycast 2.7 has no Lua built in (only its UI strings) and its command line only takes
# -config section:key=value, so it runs as a normal window on Xvfb, the presses are X key events
# sent with xdotool, and the screen is grabbed with ffmpeg x11grab at F frames per second.
# Flycast runs in real time (about 60 fps with llvmpipe), so guest frame N of a press (PC's --press
# syntax, start@700) is sent N/60 s after the Flycast window appears. The recording is not frame
# exact: scenes are aligned afterwards by content (scripts/comparar-flycast.sh).
# Keys (Flycast's default keyboard mapping): start = Return, a = x, b = c, x = s, y = d,
# up/down/left/right = arrows.
# Flycast's own config and data (emu.cfg, VMU files) live in DIR/xdg, never in the user's ~.
# Each run starts with no VMU file, so the game creates its data file as on a fresh card.
# Output in DIR (default build/flycast/serie): png/f%05d.png (640x480), p480/f%05d.png (480x272,
# box filter, whole screen, as comparar-titulo.sh scales PC), tiempos.txt ("index seconds"), and
# pulsos.txt (the presses actually sent, with their seconds after the window appeared).
# FLYCAST_BIOS=<dir with dc_boot.bin and dc_flash.bin> (the owner's real Dreamcast BIOS) is copied
# into that data directory (DIR/xdg/data/flycast) so Flycast boots with it; without it Flycast uses
# its replacement BIOS (reios), whose font is not the console's (docs/referencia-flycast.md). The
# real BIOS plays its boot animation first: the recording and the presses start
# FLYCAST_BIOS_RETRASO seconds after the window appears (default 9.7, measured: the ESP logo fades
# in 97 frames later at 10 fps), so the series indices and the scene windows of
# scripts/escenas-flycast.sh are the same as without the BIOS.
# Default P: the presses of test-psp-geometria.sh; S: 60; F: 10.
# Host needs: flatpak with org.flycast.Flycast//stable, xvfb-run, xdotool, ffmpeg, python3-PIL.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
segundos=60 fps=10 salida="" pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --pulsar) PRESSES="$2"; shift 2 ;;
    --segundos) segundos="$2"; shift 2 ;;
    --fps) fps="$2"; shift 2 ;;
    --salida) salida="$2"; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 1 ]] || { sed -n '4p' "$0" >&2; exit 2; }
CHD="$(realpath "${pos[0]}")"
[[ -f "$CHD" ]] || { echo "not a file: $CHD" >&2; exit 1; }
for t in flatpak xvfb-run xdotool ffmpeg python3; do
  command -v "$t" >/dev/null || { echo "missing host tool: $t" >&2; exit 1; }
done
flatpak info org.flycast.Flycast//stable >/dev/null 2>&1 || {
  echo "missing org.flycast.Flycast//stable" >&2; exit 1; }
OUT="$(realpath -m "${salida:-$REPO/build/flycast/serie}")"
rm -rf "$OUT/png" "$OUT/p480" "$OUT/xdg"
mkdir -p "$OUT/png" "$OUT/p480" "$OUT/xdg/config" "$OUT/xdg/data/flycast"
RETRASO=0
if [[ -n "${FLYCAST_BIOS:-}" ]]; then
  RETRASO="${FLYCAST_BIOS_RETRASO:-9.7}"
  for f in dc_boot.bin dc_flash.bin; do
    [[ -f "$FLYCAST_BIOS/$f" ]] || { echo "capturar-flycast: $FLYCAST_BIOS/$f missing" >&2; exit 1; }
    cp "$FLYCAST_BIOS/$f" "$OUT/xdg/data/flycast/$f"
  done
fi

cat > "$OUT/correr.sh" <<'SH'
#!/bin/sh
# Inside xvfb-run: start Flycast, wait for its window, record, press.
flatpak run --branch=stable --filesystem="$OUT" --filesystem="$(dirname "$CHD")":ro \
  --command=sh org.flycast.Flycast -c \
  'export XDG_CONFIG_HOME="$OUT/xdg/config" XDG_DATA_HOME="$OUT/xdg/data"; exec flycast "$CHD"' \
  > "$OUT/flycast.log" 2>&1 &
pid=$!
w=""
for i in $(seq 1 300); do
  w=$(xdotool search --name '^Flycast' 2>/dev/null | head -1)
  [ -n "$w" ] && break
  sleep 0.1
done
[ -n "$w" ] || { echo "no Flycast window" >&2; kill $pid; exit 1; }
sleep "$RETRASO"
t0=$(date +%s.%N)
ffmpeg -loglevel error -f x11grab -draw_mouse 0 -framerate "$FPS" -video_size 640x480 \
  -i "$DISPLAY" -t "$SEGUNDOS" "$OUT/png/f%05d.png" &
fp=$!
echo "$t0" > "$OUT/t0"
: > "$OUT/pulsos.txt"
for p in $(echo "$PRESSES" | tr ',' ' '); do
  b=${p%@*} n=${p#*@}
  case "$b" in
    start) k=Return ;; a) k=x ;; b) k=c ;; x) k=s ;; y) k=d ;;
    up) k=Up ;; down) k=Down ;; left) k=Left ;; right) k=Right ;;
    *) echo "unknown button $b" >&2; continue ;;
  esac
  at=$(echo "$t0 + $n / 60" | bc -l)
  now=$(date +%s.%N)
  d=$(echo "$at - $now" | bc -l)
  case "$d" in -*) ;; *) sleep "$d" ;; esac
  xdotool windowfocus "$w" 2>/dev/null
  xdotool keydown "$k"; sleep 0.1; xdotool keyup "$k"
  echo "$p $(echo "$(date +%s.%N) - $t0" | bc -l | cut -c1-7)" >> "$OUT/pulsos.txt"
done
wait $fp
kill $pid 2>/dev/null
wait $pid 2>/dev/null
exit 0
SH
export OUT CHD PRESSES RETRASO SEGUNDOS="$segundos" FPS="$fps"
timeout $(( segundos + 120 )) xvfb-run -d -s '-screen 0 640x480x24' sh "$OUT/correr.sh"

python3 - "$OUT" "$fps" <<'PY'
import os, sys
from PIL import Image
out, fps = sys.argv[1], float(sys.argv[2])
names = sorted(os.listdir(f"{out}/png"))
with open(f"{out}/tiempos.txt", "w") as t:
    for i, n in enumerate(names):
        Image.open(f"{out}/png/{n}").convert("RGB").resize((480, 272), Image.BOX).save(f"{out}/p480/{n}")
        t.write(f"{i + 1} {i / fps:.2f}\n")
print(f"capturar-flycast: {len(names)} frames in {out}")
PY
cat "$OUT/pulsos.txt"
