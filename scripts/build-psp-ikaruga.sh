#!/usr/bin/env bash
# Builds Ikaruga for the PSP from dream-recomp's core (third_party/dream-recomp plus
# patches/dream-recomp/*.patch, applied to a copy), the translated C++ in <dir-gen>, and the host
# stubs in psp/stubs, inside the pinned PSPDEV toolchain image (scripts/psp/Dockerfile).
#   scripts/build-psp-ikaruga.sh <dir-gen> [--variantes=mix,Os] [--limpio]
# Variants, each built in build/psp/ikaruga/<v>/ (OPT_RT: runtime, stubs, main.cpp; OPT_GEN: generated):
#   mix  OPT_RT=-O2 OPT_GEN=-Os  default: build/psp/ikaruga/ikaruga.elf and EBOOT.PBP
#   Os   OPT_RT=-Os OPT_GEN=-Os  build/psp/ikaruga/ikaruga-Os.elf and EBOOT-Os.PBP
#   O2   OPT_RT=-O2 OPT_GEN=-O2  build/psp/ikaruga/ikaruga-O2.elf (no EBOOT: does not fit in 64 MB)
# Default --variantes=mix,Os; --solo-o2 is --variantes=O2; --limpio drops previous objects first.
# OPT_RT / OPT_GEN in the environment override the flags of the mix variant.
# <dir-gen> is read-only mounted and never copied into the repository.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="ikaruga-psp-toolchain:v20261001"
DR="$REPO/third_party/dream-recomp"
OUT="$REPO/build/psp/ikaruga"
USAGE_LINES='2,12p'

gen="" limpio=0 variants=(mix Os)
for arg in "$@"; do
  case "$arg" in
    --limpio) limpio=1 ;;
    --solo-o2) variants=(O2) ;;
    --variantes=*) IFS=, read -r -a variants <<< "${arg#--variantes=}" ;;
    -h|--help) sed -n "$USAGE_LINES" "$0"; exit 0 ;;
    -*) echo "unknown argument: $arg" >&2; exit 2 ;;
    *) gen="$arg" ;;
  esac
done
for v in "${variants[@]}"; do
  case "$v" in mix|Os|O2) ;; *) echo "unknown variant: $v (mix, Os, O2)" >&2; exit 2 ;; esac
done
[[ -n "$gen" ]] || { sed -n "$USAGE_LINES" "$0" >&2; exit 2; }
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

opt_flags() {  # <variant> -> "OPT_RT OPT_GEN"
  case "$1" in
    mix) echo "${OPT_RT:--O2} ${OPT_GEN:--Os}" ;;
    Os)  echo "-Os -Os" ;;
    O2)  echo "-O2 -O2" ;;
  esac
}

jobs="$(nproc)"
for v in "${variants[@]}"; do
  dir="$OUT/$v"
  mkdir -p "$dir"
  read -r opt_rt opt_gen <<< "$(opt_flags "$v")"
  echo "== $v (runtime/stubs $opt_rt, generated $opt_gen) -> $dir"
  # The patched sources are rebuilt from scratch every run: drop runtime objects so they follow.
  rm -rf "$dir/obj/rt" "$dir/obj/host" "$dir/EBOOT.PBP" "$dir/PARAM.SFO"
  # make does not track flags: generated objects are reused only if built with the same OPT_GEN.
  if [[ "$(cat "$dir/opt_gen.txt" 2>/dev/null || true)" != "$opt_gen" ]]; then
    rm -rf "$dir/obj/gen" "$dir/ikaruga.elf"
  fi
  echo "$opt_gen" > "$dir/opt_gen.txt"
  targets=(ikaruga.elf)
  [[ $v != O2 ]] && targets+=(EBOOT.PBP)
  docker run --rm --user "$(id -u):$(id -g)" \
    --volume "$REPO:/repo:ro" --volume "$OUT/src:/rt:ro" --volume "$gen:/gen:ro" \
    --volume "$dir:/work" --workdir /work "$IMAGE" \
    make -j"$jobs" -f /repo/psp/ikaruga/Makefile RT=/rt GEN=/gen \
      OPT_RT="$opt_rt" OPT_GEN="$opt_gen" "${targets[@]}"
  test -s "$dir/ikaruga.elf" || { echo "$dir/ikaruga.elf was not produced" >&2; exit 1; }
  [[ $v == O2 ]] || test -s "$dir/EBOOT.PBP" || { echo "$dir/EBOOT.PBP was not produced" >&2; exit 1; }
done

for v in "${variants[@]}"; do
  case "$v" in
    mix) cp "$OUT/mix/ikaruga.elf" "$OUT/ikaruga.elf"; cp "$OUT/mix/EBOOT.PBP" "$OUT/EBOOT.PBP" ;;
    Os)  cp "$OUT/Os/ikaruga.elf" "$OUT/ikaruga-Os.elf"; cp "$OUT/Os/EBOOT.PBP" "$OUT/EBOOT-Os.PBP" ;;
    O2)  cp "$OUT/O2/ikaruga.elf" "$OUT/ikaruga-O2.elf" ;;
  esac
done

# Sizes against the ~52 MB a PSP-2000/3000 leaves to a 64 MB homebrew (docs/diseno-psp.md §2).
for v in "${variants[@]}"; do
  docker run --rm --volume "$OUT/$v:/work:ro" --workdir /work "$IMAGE" \
    psp-size ikaruga.elf | awk -v v="$v" 'NR==1{print "variant " v ": " $0; next}
      {t=$4; printf "variant %s: %s\n  image %.2f MB + guest 28.1 MB = %.2f MB of ~52 MB\n",
        v, $0, t/1048576, t/1048576+28.1}'
done
for v in "${variants[@]}"; do
  case "$v" in
    mix) echo "$OUT/ikaruga.elf"; echo "$OUT/EBOOT.PBP (runtime ${OPT_RT:--O2}, generated ${OPT_GEN:--Os})" ;;
    Os)  echo "$OUT/ikaruga-Os.elf"; echo "$OUT/EBOOT-Os.PBP (all -Os)" ;;
    O2)  echo "$OUT/ikaruga-O2.elf (all -O2)" ;;
  esac
done
