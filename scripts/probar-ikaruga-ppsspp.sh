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
GAME="$MS/PSP/GAME/IKARUGA"
mkdir -p "$GAME"
cp "$EBOOT" "$GAME/EBOOT.PBP"
if [[ "$JUEGO" != "-" ]]; then
  JUEGO="$(realpath "$JUEGO")"
  cp "$JUEGO/fs/1ST_READ.BIN" "$GAME/1ST_READ.BIN"
  "$REPO/scripts/preparar-disco-ikaruga.sh" "$JUEGO" "$GAME/DISCO.ISO" >/dev/null
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
t=0
while [ "$t" -lt "$SEGUNDOS" ] && kill -0 "$pid" 2>/dev/null; do
  if grep -q '^stop: \|^capture: ' "$GAME/ikaruga.log" 2>/dev/null; then sleep 3; break; fi
  sleep 1; t=$((t + 1))
done
alive=1
kill -0 "$pid" 2>/dev/null || alive=0
if [ -n "$CAPTURA" ] && [ $alive -eq 1 ]; then import -window root "$RAW" || true; fi
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null || true
echo "$alive" > "$WORK/alive"
SH
export APP WORK LOG RAW GAME SEGUNDOS CAPTURA
set +e
timeout $((SEGUNDOS + 60)) xvfb-run -a -s '-screen 0 480x272x24' sh "$WORK/correr.sh"
rc=$?
set -e

[[ -f "$LOG" ]] && cp "$LOG" "${SALIDA%.log}.ppsspp.log"
[[ -n "$CAPTURA" && -s "$RAW" ]] && cp "$RAW" "$CAPTURA"
[[ $rc -eq 0 ]] || { echo "probar-ikaruga-ppsspp: run failed (exit $rc)" >&2; exit 1; }
grep -q 'E\[BOOT\]: Boot failed' "$LOG" 2>/dev/null && {
  echo "probar-ikaruga-ppsspp: PPSSPP could not boot the EBOOT" >&2; exit 1; }
[[ -f "$GAME/ikaruga.log" ]] || {
  echo "probar-ikaruga-ppsspp: the EBOOT wrote no ikaruga.log" >&2; exit 1; }
cp "$GAME/ikaruga.log" "$SALIDA"
for raw in "$GAME"/captura-*.raw "$GAME"/ta-*.bin; do
  [[ -f "$raw" ]] && cp "$raw" "$(dirname "$SALIDA")/"
done
echo "PPSSPP still running at the end: $(cat "$WORK/alive" 2>/dev/null || echo ?)" >&2
echo "$SALIDA"
