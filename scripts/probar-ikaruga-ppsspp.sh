#!/usr/bin/env bash
# Runs the headless Ikaruga EBOOT in PPSSPP (flatpak org.ppsspp.PPSSPP) on a virtual X display with
# a temporary memory stick holding ms0:/PSP/GAME/IKARUGA/{EBOOT.PBP,1ST_READ.BIN,DISCO.ISO}, for
# <segundos> seconds, and copies the ikaruga.log it wrote to <salida.log>.
#   scripts/probar-ikaruga-ppsspp.sh <EBOOT.PBP> <dir-juego-extraido> <segundos> [salida.log]
# <dir-juego-extraido> has fs/ and IP.BIN (see scripts/preparar-disco-ikaruga.sh); pass "-" to run
# without game data (the EBOOT must log an error and quit). The game data is copied only into the
# temporary memory stick, which is created next to <salida.log> and deleted on exit.
# Default <salida.log>: build/psp/evidencia/ikaruga-<segundos>s.log.
# Env: PROBAR_CAPTURA=file.png also saves a screenshot at the end.
#      PROBAR_SACAR="a b" copies those files from the EBOOT's folder next to <salida.log>.
#      PROBAR_DISCO=file uses that DISCO.ISO (tools/volcar-area-hd's dump of the real disc)
#      instead of building one from <dir-juego-extraido>/fs.
#      PROBAR_CARPETA=NAME installs under ms0:/PSP/GAME/NAME instead of IKARUGA.
#      PROBAR_CFG=file is copied as ikaruga.cfg next to the EBOOT.
#      PROBAR_VMU_IN=file is copied as VMU.BIN before the run; PROBAR_VMU_OUT=file receives the
#      VMU.BIN left after it (removed if there is none).
#      PROBAR_GRABAR=DIR records the PPSSPP screen (ffmpeg x11grab, 480x272) to DIR/f%05d.png at
#      PROBAR_GRABAR_FPS frames per second (default 1) for the whole run, plus DIR/t0 (start, s).
#      PROBAR_SIN_IDLE=1 puts ms0:/PSP/GAME/IKARUGA/NO_IDLE_SKIP on the stick (idle-skip off).
#      PROBAR_CFG=file is copied to ms0:/PSP/GAME/IKARUGA/ikaruga.cfg; any captura-*.raw the EBOOT
#      writes is copied next to <salida.log>. <segundos> is then an upper bound: the run ends as
#      soon as the EBOOT quits.
# Host needs: flatpak with org.ppsspp.PPSSPP, xvfb-run, genisoimage or mkisofs, ImageMagick (import)
# for the screenshot.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -lt 3 || $# -gt 4 ]]; then
  sed -n '5p' "$0" >&2
  exit 2
fi
EBOOT="$(realpath "$1")"
JUEGO="$2"
SEGUNDOS="$3"
SALIDA="$(realpath -m "${4:-$REPO/build/psp/evidencia/ikaruga-${SEGUNDOS}s.log}")"
APP=org.ppsspp.PPSSPP

[[ -f "$EBOOT" ]] || { echo "not a file: $EBOOT" >&2; exit 1; }
[[ "$SEGUNDOS" =~ ^[0-9]+$ ]] || { echo "seconds must be a number: $SEGUNDOS" >&2; exit 2; }
for tool in flatpak xvfb-run; do
  command -v "$tool" >/dev/null || { echo "missing host tool: $tool" >&2; exit 1; }
done
flatpak info "$APP" >/dev/null 2>&1 || {
  echo "missing $APP; install with: flatpak install --user flathub $APP" >&2; exit 1; }

mkdir -p "$(dirname "$SALIDA")"
# The flatpak sees the host filesystem but has a private /tmp, so the scratch dir lives here.
WORK="$(mktemp -d "$(dirname "$SALIDA")/.ppsspp-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# PPSSPP on Linux uses $XDG_CONFIG_HOME/ppsspp as the memory stick root (PSP/SYSTEM holds its ini).
MS="$WORK/config/ppsspp"
GAME="$MS/PSP/GAME/${PROBAR_CARPETA:-IKARUGA}"
mkdir -p "$GAME"
cp "$EBOOT" "$GAME/EBOOT.PBP"
[[ -n "${PROBAR_CFG:-}" ]] && cp "$PROBAR_CFG" "$GAME/ikaruga.cfg"
[[ -n "${PROBAR_VMU_IN:-}" ]] && cp "$PROBAR_VMU_IN" "$GAME/VMU.BIN"
[[ "${PROBAR_SIN_IDLE:-0}" == 1 ]] && : > "$GAME/NO_IDLE_SKIP"
if [[ "$JUEGO" != "-" ]]; then
  JUEGO="$(realpath "$JUEGO")"
  cp "$JUEGO/fs/1ST_READ.BIN" "$GAME/1ST_READ.BIN"
  if [[ -n "${PROBAR_DISCO:-}" ]]; then
    ln "$(realpath "$PROBAR_DISCO")" "$GAME/DISCO.ISO" 2>/dev/null || cp "$PROBAR_DISCO" "$GAME/DISCO.ISO"
  else
    "$REPO/scripts/preparar-disco-ikaruga.sh" "$JUEGO" "$GAME/DISCO.ISO" >/dev/null
  fi
fi
if [[ -n "${PROBAR_CFG:-}" ]]; then
  cp "$PROBAR_CFG" "$GAME/ikaruga.cfg"
fi

# Private PPSSPP config: the user's settings are neither used nor touched. OpenGL because Xvfb has no DRI3 for Vulkan. Speed unlimited is not set: the
# PSP's own clock paces nothing here, the run is CPU bound.
mkdir -p "$WORK/config/ppsspp/PSP/SYSTEM"
cat > "$WORK/config/ppsspp/PSP/SYSTEM/ppsspp.ini" <<'INI'
[General]
FirstRun = False
[Graphics]
GraphicsBackend = 0 (OPENGL)
INI

LOG="$WORK/ppsspp.log"
RAW="$WORK/captura.png"
CAPTURA="${PROBAR_CAPTURA:-}"

cat > "$WORK/correr.sh" <<'SH'
#!/bin/sh
flatpak run --filesystem="$WORK" --command=sh "$APP" -c \
  'export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"; exec PPSSPPSDL --windowed --xres 480 --yres 272 --log="$LOG" "$GAME/EBOOT.PBP"' \
  >"$WORK/stdout.log" 2>&1 &
pid=$!
gp=""
if [ -n "$GRABAR" ]; then
  mkdir -p "$GRABAR"; date +%s.%N > "$GRABAR/t0"
  ffmpeg -loglevel error -f x11grab -draw_mouse 0 -framerate "$GRABAR_FPS" -video_size 480x272 \
    -i "$DISPLAY" "$GRABAR/f%05d.png" </dev/null &
  gp=$!
fi
t=0
while [ "$t" -lt "$SEGUNDOS" ] && kill -0 "$pid" 2>/dev/null; do
  if grep -q '^stop: \|^capture: ' "$GAME/ikaruga.log" 2>/dev/null; then sleep 3; break; fi
  sleep 1; t=$((t + 1))
done
alive=1
kill -0 "$pid" 2>/dev/null || alive=0
if [ -n "$CAPTURA" ] && [ $alive -eq 1 ]; then import -window root "$RAW" || true; fi
[ $alive -eq 1 ] && kill "$pid" 2>/dev/null
[ -n "$gp" ] && { kill "$gp" 2>/dev/null; wait "$gp" 2>/dev/null; }
wait "$pid" 2>/dev/null
echo $? > "$WORK/ppsspp-rc"
echo "$alive" > "$WORK/alive"
SH
GRABAR="${PROBAR_GRABAR:+$(realpath -m "$PROBAR_GRABAR")}" GRABAR_FPS="${PROBAR_GRABAR_FPS:-1}"
export APP WORK LOG RAW GAME SEGUNDOS CAPTURA GRABAR GRABAR_FPS
set +e
# -d: Xvfb picks a free display itself; -a can give two parallel runs the same one, and the
# first to end then kills the X server under the other.
timeout $((SEGUNDOS + 60)) xvfb-run -d -s '-screen 0 480x272x24' sh "$WORK/correr.sh"
rc=$?
set -e

# Logs and files the EBOOT wrote are copied before any verdict, also when the run failed.
[[ -f "$LOG" ]] && cp "$LOG" "${SALIDA%.log}.ppsspp.log"
[[ -f "$WORK/stdout.log" ]] && cp "$WORK/stdout.log" "${SALIDA%.log}.ppsspp-stdout.log"
[[ -n "$CAPTURA" && -s "$RAW" ]] && cp "$RAW" "$CAPTURA"
[[ -f "$GAME/ikaruga.log" ]] && cp "$GAME/ikaruga.log" "$SALIDA"
for raw in "$GAME"/captura-*.raw "$GAME"/ta-*.bin; do
  [[ -f "$raw" ]] && cp "$raw" "$(dirname "$SALIDA")/"
done
ALIVE="$(cat "$WORK/alive" 2>/dev/null || echo ?)"
PPSSPP_RC="$(cat "$WORK/ppsspp-rc" 2>/dev/null || echo ?)"
[[ "$ALIVE" == 1 ]] && PPSSPP_RC="$PPSSPP_RC (killed by this script)"
echo "PPSSPP exit code: $PPSSPP_RC; still running at the end: $ALIVE; xvfb-run/timeout exit: $rc" >&2
[[ $rc -eq 0 ]] || { echo "probar-ikaruga-ppsspp: run failed (exit $rc)" >&2; exit 1; }
grep -q 'E\[BOOT\]: Boot failed' "$LOG" 2>/dev/null && {
  echo "probar-ikaruga-ppsspp: PPSSPP could not boot the EBOOT" >&2; exit 1; }
[[ -f "$GAME/ikaruga.log" ]] || {
  echo "probar-ikaruga-ppsspp: the EBOOT wrote no ikaruga.log" >&2; exit 1; }
for f in ${PROBAR_SACAR:-}; do
  [[ -f "$GAME/$f" ]] && cp "$GAME/$f" "$(dirname "$SALIDA")/$f"
done
if [[ -n "${PROBAR_VMU_OUT:-}" ]]; then
  rm -f "$PROBAR_VMU_OUT"
  [[ -f "$GAME/VMU.BIN" ]] && cp "$GAME/VMU.BIN" "$PROBAR_VMU_OUT"
fi
echo "$SALIDA"
