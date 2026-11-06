#!/usr/bin/env bash
# Acceptance test for the PSP toolchain and the PPSSPP runner:
#   1. scripts/build-psp.sh --limpio produces build/psp/hola/EBOOT.PBP
#   2. scripts/probar-ppsspp.sh boots it and OCR reads "ikaruga-psp" in the capture
#   3. a truncated EBOOT makes scripts/probar-ppsspp.sh fail
#   scripts/test-psp-hola.sh [--sin-limpio]   (--sin-limpio reuses the toolchain image)
# Host needs, besides what the two scripts need: tesseract.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/build/test-psp-hola"
EBOOT="$REPO/build/psp/hola/EBOOT.PBP"
ESPERADO="ikaruga-psp"

build_args=(--limpio)
[[ "${1:-}" == "--sin-limpio" ]] && build_args=()

command -v tesseract >/dev/null || { echo "missing host tool: tesseract" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
fallos=0
ok()  { echo "PASS: $*"; }
mal() { echo "FAIL: $*"; fallos=$((fallos + 1)); }

# 1. Build from scratch.
"$REPO/scripts/build-psp.sh" "${build_args[@]}" >"$OUT/build.log" 2>&1 \
  && [[ -s "$EBOOT" ]] && ok "build-psp.sh ${build_args[*]} -> $EBOOT" \
  || { mal "build-psp.sh (see $OUT/build.log)"; exit 1; }

# 2. Boot and read the text back.
if "$REPO/scripts/probar-ppsspp.sh" "$EBOOT" "$OUT/hola.png" >"$OUT/probar.log" 2>&1; then
  ok "probar-ppsspp.sh boots the EBOOT"
  # The debug screen font is 8x8 pixels: crop to the drawn pixels, upscale with
  # nearest neighbour, dark-on-light, then read one line restricted to the
  # characters the text can contain.
  magick "$OUT/hola.png" -trim +repage -filter point -resize 600% -negate \
    -bordercolor white -border 40 "$OUT/ocr.png"
  leido="$(tesseract "$OUT/ocr.png" - --psm 7 \
    -c tessedit_char_whitelist=abcdefghijklmnopqrstuvwxyz- 2>/dev/null | tr -d '[:space:]')"
  [[ "$leido" == "$ESPERADO" ]] && ok "OCR reads '$leido'" || mal "OCR read '$leido', expected '$ESPERADO'"
else
  mal "probar-ppsspp.sh on the good EBOOT (see $OUT/probar.log)"
fi

# 3. Truncated EBOOT must be rejected.
head -c "$(( $(stat -c %s "$EBOOT") / 2 ))" "$EBOOT" >"$OUT/truncado.PBP"
if "$REPO/scripts/probar-ppsspp.sh" "$OUT/truncado.PBP" "$OUT/truncado.png" >"$OUT/truncado.log" 2>&1; then
  mal "probar-ppsspp.sh accepted a truncated EBOOT"
else
  ok "probar-ppsspp.sh rejects a truncated EBOOT (exit $?)"
fi

[[ $fallos -eq 0 ]] && echo "all passed" || { echo "$fallos failed"; exit 1; }
