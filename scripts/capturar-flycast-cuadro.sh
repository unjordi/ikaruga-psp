#!/usr/bin/env bash
# Flycast's image at exact guest frames, with the presses on exact frames: the core of Flycast in
# RetroArch (flatpak org.libretro.RetroArch, user install) plays a replay whose joypad input
# scripts/flycast-replay.py rewrote, and RetroArch's --max-frames-ss takes the screenshot.
#   scripts/capturar-flycast-cuadro.sh <disco.chd> --cuadros N[,N...] [--pulsar P] [--retraso K]
#                                       [--salida DIR] [--paralelo J]
# Guest frame N (PC's --press numbering: start@700 is held 8 frames from N=700) is the screen after
# the core ran K+N frames; K (default 700) is at least the frames the real BIOS and the game's own loading
# spends before the game's frame 0. Output in DIR (default build/flycast/cuadros): png/f<N>.png
# (640x480 as the core outputs it), p480/f<N>.png (480x272, box filter, as capturar-flycast.sh).
# Everything deterministic: the same arguments give the same images (byte for byte).
# Needs FLYCAST_BIOS=<dir with dc_boot.bin and dc_flash.bin> (the owner's BIOS), xvfb-run, flatpak
# with org.libretro.RetroArch (its flycast_libretro.so core), python3-PIL. RetroArch's config,
# core options and saves are per run (build/flycast/ra), never in the user's RetroArch.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESSES="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
K="${FLYCAST_RETRASO_CUADROS:-700}" cuadros="" salida="" J=3 pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cuadros) cuadros="$2"; shift 2 ;;
    --pulsar) PRESSES="$2"; shift 2 ;;
    --retraso) K="$2"; shift 2 ;;
    --salida) salida="$2"; shift 2 ;;
    --paralelo) J="$2"; shift 2 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 1 && "$cuadros" =~ ^[0-9]+(,[0-9]+)*$ ]] || { sed -n '4,5p' "$0" >&2; exit 2; }
CHD="$(realpath "${pos[0]}")"
[[ -f "$CHD" ]] || { echo "not a file: $CHD" >&2; exit 1; }
: "${FLYCAST_BIOS:?capturar-flycast-cuadro: set FLYCAST_BIOS=<dir with dc_boot.bin and dc_flash.bin>}"
for f in dc_boot.bin dc_flash.bin; do
  [[ -f "$FLYCAST_BIOS/$f" ]] || { echo "capturar-flycast-cuadro: $FLYCAST_BIOS/$f missing" >&2; exit 1; }
done
for t in flatpak xvfb-run python3; do
  command -v "$t" >/dev/null || { echo "missing host tool: $t" >&2; exit 1; }
done
flatpak info org.libretro.RetroArch >/dev/null 2>&1 || { echo "missing org.libretro.RetroArch (flatpak install --user flathub org.libretro.RetroArch)" >&2; exit 1; }
CORE="$HOME/.var/app/org.libretro.RetroArch/config/retroarch/cores/flycast_libretro.so"
[[ -f "$CORE" ]] || { echo "missing $CORE (RetroArch online updater: Flycast core)" >&2; exit 1; }

OUT="$(realpath -m "${salida:-$REPO/build/flycast/cuadros}")"
R="$REPO/build/flycast/ra"
mkdir -p "$OUT/png" "$OUT/p480" "$R"

IFS=, read -ra LISTA <<< "$cuadros"
MAX=0; for n in "${LISTA[@]}"; do (( n > MAX )) && MAX=$n; done
TOTAL=$(( K + MAX + 2 )); (( TOTAL < 4200 )) && TOTAL=4200
export R CHD CORE K OUT FLYCAST_OPCIONES FLYCAST_ESTADOS

# Every RetroArch run gets a fresh directory with only the BIOS: the core writes VMU files and
# dc_nvmem.bin next to the BIOS, and a leftover VMU changes the game's first screen ("load" instead
# of "create the data file").
correr() { # <args de retroarch...>
  local w; w="$(mktemp -d "$R/w.XXXXXX")"
  mkdir -p "$w/system/dc" "$w/saves" "$w/states" "$w/shots" "$w/cfg/retroarch" "$w/cfgdir/Flycast"
  cp "$FLYCAST_BIOS/dc_boot.bin" "$FLYCAST_BIOS/dc_flash.bin" "$w/system/dc/"
  cat > "$w/cfg/retroarch/retroarch.cfg" <<CFG
system_directory = "$w/system"
savefile_directory = "$w/saves"
savestate_directory = "$w/states"
screenshot_directory = "$w/shots"
rgui_config_directory = "$w/cfgdir"
video_driver = "gl"
video_scale = "1.0"
video_vsync = "false"
video_fullscreen = "false"
video_gpu_screenshot = "false"
audio_driver = "null"
audio_enable = "false"
config_save_on_exit = "false"
savestate_auto_save = "${FLYCAST_ESTADOS:+true}"
savestate_file_compression = "false"
pause_nonactive = "false"
video_font_enable = "false"
CFG
  cat > "$w/cfgdir/Flycast/Flycast.opt" <<OPT
reicast_internal_resolution = "640x480"
reicast_threaded_rendering = "disabled"
reicast_region = "Japan"
reicast_language = "Japanese"
OPT
  local kv
  for kv in ${FLYCAST_OPCIONES:-}; do echo "${kv%%=*} = \"${kv#*=}\"" >> "$w/cfgdir/Flycast/Flycast.opt"; done
  local rc=0
  timeout 600 xvfb-run -a -s '-screen 0 640x480x24' flatpak run --filesystem="$R" --filesystem="$OUT" \
    --filesystem="$(dirname "$CHD")":ro --command=retroarch org.libretro.RetroArch \
    -c "$w/cfg/retroarch/retroarch.cfg" -L "$CORE" "$@" || rc=$?
  [[ -n "${FLYCAST_ESTADOS:-}" && -n "${ESTADO_NOMBRE:-}" ]] && mkdir -p "$FLYCAST_ESTADOS" && find "$w/states" -type f ! -name "*.png" -exec cp {} "$FLYCAST_ESTADOS/$ESTADO_NOMBRE" \;
  rm -rf "$w"
  return $rc
}
export -f correr

# One replay without input (the game boots from reset, so it is the same for every press list).
BASE="$R/base-$TOTAL-$(printf %s "${FLYCAST_OPCIONES:-}" | md5sum | cut -c1-8).replay"
if [[ ! -s "$BASE" ]]; then
  rm -f "$R"/base-*.replay
  correr -R "$BASE" --max-frames="$TOTAL" "$CHD" > "$R/base.log" 2>&1
fi
python3 -I "$REPO/scripts/flycast-replay.py" "$BASE" "$OUT/entrada.replay" "$PRESSES" --offset "$K"

una() { # <N>: screenshot after K+N frames; xvfb-run -a can collide when several start together
  local n="$1" f=$(( K + $1 )) try
  export ESTADO_NOMBRE="f$n.state"
  for try in 1 2 3; do
    rm -f "$OUT/png/f$n.png"
    correr -P "$OUT/entrada.replay" --max-frames="$f" --max-frames-ss \
      --max-frames-ss-path="$OUT/png/f$n.png" "$CHD" > "$R/cuadro-$n.log" 2>&1 || true
    [[ -s "$OUT/png/f$n.png" ]] && return 0
    sleep $(( RANDOM % 4 + 1 ))
  done
  echo "capturar-flycast-cuadro: no screenshot for frame $n (see $R/cuadro-$n.log)" >&2; return 1
}
export -f una
printf '%s\n' "${LISTA[@]}" | xargs -P "$J" -I{} bash -c 'una {}'

python3 -I - "$OUT" "${LISTA[@]}" <<'PY'
import sys
from PIL import Image
out = sys.argv[1]
for n in sys.argv[2:]:
    im = Image.open(f"{out}/png/f{n}.png").convert("RGB")
    assert im.size == (640, 480), im.size
    im.resize((480, 272), Image.BOX).save(f"{out}/p480/f{n}.png")
print(f"capturar-flycast-cuadro: {len(sys.argv) - 2} frames in {out}")
PY
