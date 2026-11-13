#!/usr/bin/env bash
# Runs every PSP test in order and prints one line per test (ok / FAIL and its last line), then the
# count. This is the battery that runs when a change is integrated.
#   scripts/bateria.sh <EBOOT.PBP> <dir-juego-extraido>
#                      [--double EBOOT-double.PBP] [--sin-velocidad] [--solo name,name]
# --double runs test-psp-igual-pc-double.sh with that EBOOT (built with DREAM_FPU_DOUBLE=1).
# Env: PROBAR_DISCO (required, real disc dump) and DREAM_RECOMP (the double PC launcher's checkout)
# as each test needs them. Outputs: build/psp/evidencia/bateria/<name>.out and resumen.txt.
# The image tests (titulo, video-*, texturas-juego) compare against Flycast's image aligned with
# each guest frame (scripts/flycast-alineado.sh; needs FLYCAST_BIOS, recorded on first use);
# geometria against the Flycast reference series. test-psp-video.sh (c)
# is test-psp-titulo.sh (a) (same EBOOT, frame 1243): only (a) and (b) run.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
args=() double="" velocidad=1 solo=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --double) double="$2"; shift 2 ;;
    --sin-velocidad) velocidad=0; shift ;;
    --solo) solo=",$2,"; shift 2 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) args+=("$1"); shift ;;
  esac
done
[[ ${#args[@]} -eq 2 ]] || { sed -n '4,5p' "$0" >&2; exit 2; }
[[ -n "${PROBAR_DISCO:-}" ]] || { echo "bateria: set PROBAR_DISCO (docs/primer-uso.md)" >&2; exit 2; }
E="${args[0]}" G="${args[1]}"
OUT="$REPO/build/psp/evidencia/bateria"
rm -rf "$OUT"; mkdir -p "$OUT"
fallos=0 total=0

run() {  # run <name> <command...>
  local name="$1"; shift
  [[ -n "$solo" && "$solo" != *",$name,"* ]] && return 0
  total=$((total + 1))
  local t0=$SECONDS
  if "$@" >"$OUT/$name.out" 2>&1; then r="ok"; else r="FAIL"; fallos=$((fallos + 1)); fi
  printf '%-18s %-4s %4ss  %s\n' "$name" "$r" $((SECONDS - t0)) "$(grep -v '^\s*$' "$OUT/$name.out" | tail -1 | cut -c1-90)" | tee -a "$OUT/resumen.txt"
}

S="$REPO/scripts"
run gitignore "$S/test-gitignore.sh"
run arranque "$S/test-psp-arranque.sh" "$E" "$G"
run redondeo env -u DREAM_RECOMP "$S/test-psp-redondeo.sh"
run rtc "$S/test-psp-rtc.sh" "$E" "$G"
run carpeta "$S/test-psp-carpeta.sh" "$E" "$G"
run vmu "$S/test-psp-vmu.sh" "$E" "$G"
run salida-suspension "$S/test-psp-salida-suspension.sh" "$E" "$G"
run igual-pc "$S/test-psp-igual-pc.sh" "$E" "$G"
run geometria "$S/test-psp-geometria.sh" "$E" "$G"
run titulo "$S/test-psp-titulo.sh" "$E" "$G"
run video-recorte "$S/test-psp-video.sh" "$E" "$G" --solo a
run video-niebla "$S/test-psp-video.sh" "$E" "$G" --solo b
run texturas-juego "$S/test-psp-texturas-juego.sh" "$E" "$G"
[[ -n "$double" ]] && run igual-pc-double "$S/test-psp-igual-pc-double.sh" "$double" "$G"
[[ $velocidad -eq 1 ]] && run velocidad "$S/medir-velocidad.sh" "$E" "$G" --veces 1 --solo 2600
echo "$((total - fallos))/$total ok" | tee -a "$OUT/resumen.txt"
[[ $fallos -eq 0 ]]
