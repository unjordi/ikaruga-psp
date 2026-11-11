#!/usr/bin/env bash
# Game data and captures must stay out of git wherever they land in the tree; the project sources
# must stay in. Checks names with git check-ignore, so it needs no game files.
#   scripts/test-gitignore.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
fallos=0
for p in DISCO.ISO disco.iso memstick/PSP/GAME/IKARUGA/DISCO.ISO 1ST_READ.BIN IP.BIN juego/Ikaruga.chd \
         captura-2600.raw captura-2600.png psp-1243.png pc-1243-480x272.png flycast-1243.png \
         VMU.BIN VMU.BIN.TMP pc.vmu ikaruga.cfg ikaruga.log gen/ikaruga.cpp build/x EBOOT.PBP; do
  git check-ignore -q --no-index "$p" || { echo "FAIL: not ignored: $p"; fallos=$((fallos + 1)); }
done
while read -r f; do
  if git check-ignore -q --no-index "$f"; then echo "FAIL: tracked file matches .gitignore: $f"; fallos=$((fallos + 1)); fi
done < <(git ls-files)
[[ $fallos -eq 0 ]] && echo "all passed" || { echo "$fallos failed"; exit 1; }
