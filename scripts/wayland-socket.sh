#!/usr/bin/env bash
# Prints the Wayland display to use: $WAYLAND_DISPLAY if set, else the first wayland-N socket in
# $XDG_RUNTIME_DIR (default /run/user/<uid>). Exits 1 with a message if there is none.
set -euo pipefail
[[ -n "${WAYLAND_DISPLAY:-}" ]] && { echo "$WAYLAND_DISPLAY"; exit 0; }
rt="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
for s in "$rt"/wayland-[0-9]*; do
  [[ -S "$s" ]] && { basename "$s"; exit 0; }
done
echo "no Wayland socket in $rt (the PC window needs a Wayland session)" >&2
exit 1
