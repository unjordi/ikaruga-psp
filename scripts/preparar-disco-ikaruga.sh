#!/usr/bin/env bash
# Builds the DISCO.ISO the PSP build reads as the GD-ROM's game area (psp/stubs/disc.cpp) from an
# extracted Ikaruga disc: <dir>/fs is the file system, <dir>/IP.BIN goes in the system area. The
# image's sector numbers start at LBA 45000, where a GD-ROM's high-density area begins, so the
# directory records the game reads point at the right sectors.
#   scripts/preparar-disco-ikaruga.sh <dir-extraido> <DISCO.ISO>
# Host needs: genisoimage or mkisofs.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  sed -n '5p' "$0" >&2
  exit 2
fi
src="$1" out="$2"
[[ -d "$src/fs" ]] || { echo "$src/fs: not a directory" >&2; exit 1; }
[[ -f "$src/IP.BIN" ]] || { echo "$src/IP.BIN: missing" >&2; exit 1; }
[[ -f "$src/fs/1ST_READ.BIN" ]] || { echo "$src/fs/1ST_READ.BIN: missing" >&2; exit 1; }
mk="$(command -v genisoimage || command -v mkisofs || true)"
[[ -n "$mk" ]] || { echo "missing host tool: genisoimage or mkisofs" >&2; exit 1; }

# -C 0,45000: lay the session out as if it started at sector 45000; -G: the 16-sector system area.
"$mk" -quiet -C 0,45000 -V IKARUGA -G "$src/IP.BIN" -l -J -r -o "$out" "$src/fs"
echo "$out"
