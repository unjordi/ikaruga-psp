#!/usr/bin/env bash
# Rounding-mode test for the PSP build of dream::fenv, in PPSSPP: builds psp/pruebas/redondeo
# against the runtime patched with patches/dream-recomp/*.patch, runs it and requires every check
# to pass (toward-zero truncates 1/3, nearest rounds it up, current() reads both back).
#   scripts/test-psp-redondeo.sh [dir-parches (default patches/dream-recomp)]
# Env: DREAM_RECOMP=<dream-recomp checkout> instead of third_party/dream-recomp.
# Log: build/psp/evidencia/test-redondeo.log.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="ikaruga-psp-toolchain:v20261001"
DR="${DREAM_RECOMP:-$REPO/third_party/dream-recomp}"
PARCHES="$(realpath "${1:-$REPO/patches/dream-recomp}")"
OUT="$REPO/build/psp/redondeo"
EVID="$REPO/build/psp/evidencia"

[[ -f "$DR/runtime/src/fenv.cpp" ]] || { echo "no dream-recomp runtime in $DR" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/src" "$OUT/work" "$EVID"
cp -r "$DR/runtime" "$DR/render" "$OUT/src/"
for p in "$PARCHES"/*.patch; do
  patch --quiet --directory="$OUT/src" -p1 < "$p"
done
docker run --rm --user "$(id -u):$(id -g)" \
  --volume "$REPO:/repo:ro" --volume "$OUT/src:/rt:ro" --volume "$OUT/work:/work" \
  --workdir /work "$IMAGE" make -f /repo/psp/pruebas/redondeo/Makefile RT=/rt EBOOT.PBP >"$OUT/build.log" 2>&1 ||
  { echo "build failed, see $OUT/build.log" >&2; exit 1; }

log="$EVID/test-redondeo.log"
"$REPO/scripts/probar-ikaruga-ppsspp.sh" "$OUT/work/EBOOT.PBP" - 8 "$log" >/dev/null 2>&1 || true
[[ -s "$log" ]] || { echo "FAIL: the probe wrote no log" >&2; exit 1; }
cat "$log"
grep -q '^result: all passed' "$log" || { echo "FAIL: rounding mode not honoured" >&2; exit 1; }
echo "ok"
