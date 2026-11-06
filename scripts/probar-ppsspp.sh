#!/usr/bin/env bash
# Boots an EBOOT.PBP in PPSSPP (flatpak org.ppsspp.PPSSPP) on a virtual X
# display, waits a few seconds and saves a screenshot of the PSP screen.
#   scripts/probar-ppsspp.sh <EBOOT.PBP> <captura.png>
# Exits non-zero if the emulator reports a failed boot, dies early, or the
# screenshot is a single flat color.
# Env: PROBAR_SEGUNDOS (default 8) seconds to run before the capture.
# Host needs: flatpak with org.ppsspp.PPSSPP, xvfb-run, ImageMagick (import, magick).
set -euo pipefail

if [[ $# -ne 2 ]]; then
  sed -n '4p' "$0" >&2
  exit 2
fi
EBOOT="$(realpath "$1")"
CAPTURA="$(realpath -m "$2")"
SEGUNDOS="${PROBAR_SEGUNDOS:-8}"
APP=org.ppsspp.PPSSPP

[[ -f "$EBOOT" ]] || { echo "not a file: $EBOOT" >&2; exit 1; }
for tool in flatpak xvfb-run import magick; do
  command -v "$tool" >/dev/null || { echo "missing host tool: $tool" >&2; exit 1; }
done
flatpak info "$APP" >/dev/null 2>&1 || {
  echo "missing $APP; install with: flatpak install --user flathub $APP" >&2; exit 1; }

mkdir -p "$(dirname "$CAPTURA")"
# Scratch dir next to the capture: the flatpak sees the host filesystem but has a private /tmp.
WORK="$(mktemp -d "$(dirname "$CAPTURA")/.ppsspp-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# Private PPSSPP config so the user's settings are neither used nor touched.
# OpenGL: Xvfb has no DRI3, so the Vulkan backend cannot present.
mkdir -p "$WORK/config/ppsspp/PSP/SYSTEM"
cat > "$WORK/config/ppsspp/PSP/SYSTEM/ppsspp.ini" <<'INI'
[General]
FirstRun = False
[Graphics]
GraphicsBackend = 0 (OPENGL)
INI

LOG="$WORK/ppsspp.log"
RAW="$WORK/captura.png"

# Inside xvfb-run: start the emulator, wait, check it is alive, grab the root window.
# The screen is exactly the PSP resolution so the window covers it 1:1.
cat > "$WORK/correr.sh" <<'SH'
#!/bin/sh
flatpak run --command=sh "$APP" -c \
  'export XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data"; exec PPSSPPSDL --windowed --xres 480 --yres 272 --log="$LOG" "$EBOOT"' \
  >"$WORK/stdout.log" 2>&1 &
pid=$!
sleep "$SEGUNDOS"
if ! kill -0 "$pid" 2>/dev/null; then
  echo "PPSSPP exited before the capture" >&2
  exit 3
fi
import -window root "$RAW"
kill "$pid"
wait "$pid" 2>/dev/null || true
SH
export APP WORK LOG RAW EBOOT SEGUNDOS
set +e
timeout $((SEGUNDOS + 60)) xvfb-run -d -s '-screen 0 480x272x24' sh "$WORK/correr.sh"
rc=$?
set -e

fallo() {
  echo "probar-ppsspp: $*" >&2
  [[ -f "$LOG" ]] && grep -E ' E\[' "$LOG" | head -20 >&2
  exit 1
}

[[ $rc -eq 0 ]] || fallo "run failed (exit $rc)"
[[ -f "$LOG" ]] || fallo "PPSSPP wrote no log"
grep -q 'E\[BOOT\]: Boot failed' "$LOG" && fallo "PPSSPP could not boot $EBOOT"
grep -q 'N\[BOOT\]: .*Booted ' "$LOG" || fallo "PPSSPP never reported a successful boot"
[[ -s "$RAW" ]] || fallo "no screenshot"
colores="$(magick "$RAW" -format '%k' info:)"
[[ "$colores" -gt 1 ]] || fallo "screenshot is a single flat color"

mv "$RAW" "$CAPTURA"
echo "$CAPTURA"
