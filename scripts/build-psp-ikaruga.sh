#!/usr/bin/env bash
# Builds build/psp/ikaruga/ikaruga.elf from dream-recomp's core (third_party/dream-recomp plus
# patches/dream-recomp/*.patch, applied to a copy), the translated C++ in <dir-gen>, and the host
# stubs in psp/stubs, inside the pinned PSPDEV toolchain image (scripts/psp/Dockerfile).
#   scripts/build-psp-ikaruga.sh <dir-gen>             -O2 (ikaruga.elf) and -Os (ikaruga-Os.elf,
#                                                       EBOOT.PBP)
#   scripts/build-psp-ikaruga.sh <dir-gen> --solo-o2   only the -O2 variant
#   scripts/build-psp-ikaruga.sh <dir-gen> --solo-os   only the -Os variant (EBOOT.PBP)
#   scripts/build-psp-ikaruga.sh <dir-gen> --limpio    drop previous objects first
# <dir-gen> is read-only mounted and never copied into the repository.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="ikaruga-psp-toolchain:v20261001"
DR="$REPO/third_party/dream-recomp"
OUT="$REPO/build/psp/ikaruga"

gen="" limpio=0 variants=(O2 Os)
for arg in "$@"; do
  case "$arg" in
    --limpio) limpio=1 ;;
    --solo-o2) variants=(O2) ;;
    --solo-os) variants=(Os) ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $arg" >&2; exit 2 ;;
    *) gen="$arg" ;;
  esac
done
[[ -n "$gen" ]] || { sed -n '2,9p' "$0" >&2; exit 2; }
gen="$(cd "$gen" && pwd)"
compgen -G "$gen/*.cpp" >/dev/null || { echo "$gen: no .cpp files" >&2; exit 1; }
command -v docker >/dev/null || { echo "docker not found" >&2; exit 1; }
[[ -f "$DR/runtime/src/system.cpp" ]] || { echo "submodule missing: git submodule update --init third_party/dream-recomp" >&2; exit 1; }

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build --tag "$IMAGE" --file "$REPO/scripts/psp/Dockerfile" "$REPO/scripts/psp"
fi

[[ $limpio -eq 1 ]] && rm -rf "$OUT"
mkdir -p "$OUT"

# Patched copy of the parts of the runtime we compile; the submodule itself stays untouched.
rm -rf "$OUT/src"
mkdir -p "$OUT/src"
cp -r "$DR/runtime" "$DR/render" "$OUT/src/"
for p in "$REPO"/patches/dream-recomp/*.patch; do
  patch --quiet --directory="$OUT/src" -p1 < "$p"
done

jobs="$(nproc)"
for v in "${variants[@]}"; do
  dir="$OUT/$v"
  mkdir -p "$dir"
  echo "== -$v -> $dir"
  # The patched sources are rebuilt from scratch every run: drop runtime objects so they follow.
  rm -rf "$dir/obj/rt" "$dir/obj/host" "$dir/EBOOT.PBP" "$dir/PARAM.SFO"
  targets=(ikaruga.elf)
  [[ $v == Os ]] && targets+=(EBOOT.PBP)
  docker run --rm --user "$(id -u):$(id -g)" \
    --volume "$REPO:/repo:ro" --volume "$OUT/src:/rt:ro" --volume "$gen:/gen:ro" \
    --volume "$dir:/work" --workdir /work "$IMAGE" \
    make -j"$jobs" -f /repo/psp/ikaruga/Makefile RT=/rt GEN=/gen OPT=-"$v" "${targets[@]}"
  test -s "$dir/ikaruga.elf" || { echo "$dir/ikaruga.elf was not produced" >&2; exit 1; }
done

[[ -s "$OUT/O2/ikaruga.elf" ]] && cp "$OUT/O2/ikaruga.elf" "$OUT/ikaruga.elf"
if [[ -s "$OUT/Os/ikaruga.elf" ]]; then
  cp "$OUT/Os/ikaruga.elf" "$OUT/ikaruga-Os.elf"
  test -s "$OUT/Os/EBOOT.PBP" || { echo "$OUT/Os/EBOOT.PBP was not produced" >&2; exit 1; }
  cp "$OUT/Os/EBOOT.PBP" "$OUT/EBOOT.PBP"
fi

# Sizes against the ~52 MB a PSP-2000/3000 leaves to a 64 MB homebrew (docs/diseno-psp.md §2).
for v in "${variants[@]}"; do
  docker run --rm --volume "$OUT/$v:/work:ro" --workdir /work "$IMAGE" \
    psp-size ikaruga.elf | awk -v v="$v" 'NR==1{print "variant " v ": " $0; next}
      {t=$4; printf "variant %s: %s\n  image %.2f MB + guest 28.1 MB = %.2f MB of ~52 MB\n",
        v, $0, t/1048576, t/1048576+28.1}'
done
[[ -s "$OUT/ikaruga.elf" ]] && echo "$OUT/ikaruga.elf"
[[ -s "$OUT/EBOOT.PBP" ]] && echo "$OUT/EBOOT.PBP (-Os)"
