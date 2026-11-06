#!/usr/bin/env bash
# Builds psp/hola into build/psp/hola/EBOOT.PBP inside a Docker image with a
# pinned PSPDEV toolchain (see scripts/psp/Dockerfile).
#   scripts/build-psp.sh            reuse the toolchain image if present
#   scripts/build-psp.sh --limpio   drop the image and its build cache, rebuild from scratch
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="ikaruga-psp-toolchain:v20261001"
SRC="$REPO/psp/hola"
OUT="$REPO/build/psp/hola"

limpio=0
for arg in "$@"; do
  case "$arg" in
    --limpio) limpio=1 ;;
    -h|--help) sed -n '2,5p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

command -v docker >/dev/null || { echo "docker not found" >&2; exit 1; }

if [[ $limpio -eq 1 ]]; then
  # Only our image; the machine-wide build cache is left alone, --no-cache below skips it.
  docker image rm --force "$IMAGE" >/dev/null 2>&1 || true
  rm -rf "$OUT"
fi

if [[ $limpio -eq 1 ]] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  cache=()
  [[ $limpio -eq 1 ]] && cache=(--no-cache --pull)
  docker build "${cache[@]}" \
    --tag "$IMAGE" --file "$REPO/scripts/psp/Dockerfile" "$REPO/scripts/psp"
fi

# Objects go to build/, never next to the sources.
mkdir -p "$OUT"
cp "$SRC"/*.c "$SRC/Makefile" "$OUT/"
rm -f "$OUT/EBOOT.PBP"

docker run --rm --user "$(id -u):$(id -g)" \
  --volume "$OUT:/work" --workdir /work "$IMAGE" make

test -s "$OUT/EBOOT.PBP" || { echo "EBOOT.PBP was not produced" >&2; exit 1; }
echo "$OUT/EBOOT.PBP"
