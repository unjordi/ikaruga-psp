#!/usr/bin/env bash
# Flycast's images aligned with PSP guest frame N: frames N+D-2 .. N+D+2 of
# scripts/capturar-flycast-cuadro.sh, with D = DESFASE_<N> of scripts/escenas-flycast.sh
# (docs/referencia-flycast.md, «Desfase de la línea de tiempo»). Records the missing ones and
# prints one path per line (480x272 PNG), for comparar-flycast.sh --imagen.
#   scripts/flycast-alineado.sh <N> [--pulsar P] [--desfase-var NAME]
# P: the presses the PSP ran with (default: PRESSES_CAP1). DESFASE_<N> is measured for those
# presses; with other presses use DESFASE_<N>_<tag> via --desfase-var NAME.
# Cache: build/flycast/alineado/<md5 of P>/ (gitignored). Exit 2 when DESFASE_<N> is not defined
# (measure it first, docs/referencia-flycast.md), 1 when a capture fails.
# Env: FLYCAST_BIOS and FLYCAST_CHD (default build/flycast/Ikaruga.chd), as capturar-flycast-cuadro.sh.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/scripts/escenas-flycast.sh"
P="$PRESSES_CAP1" var="" pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --pulsar) P="$2"; shift 2 ;;
    --desfase-var) var="$2"; shift 2 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 1 && "${pos[0]}" =~ ^[0-9]+$ ]] || { sed -n '6p' "$0" >&2; exit 2; }
n="${pos[0]}"
var="${var:-DESFASE_$n}"
[[ -n "${!var:-}" ]] || { echo "flycast-alineado: $var not defined in scripts/escenas-flycast.sh (measure it: docs/referencia-flycast.md)" >&2; exit 2; }
d="${!var}"
dir="$REPO/build/flycast/alineado/$(printf '%s' "$P" | md5sum | cut -c1-12)"
faltan=() lista=()
for f in $(seq $(( n + d - 2 )) $(( n + d + 2 ))); do
  lista+=("$dir/p480/f$f.png")
  [[ -s "$dir/p480/f$f.png" ]] || faltan+=("$f")
done
if [[ ${#faltan[@]} -gt 0 ]]; then
  chd="${FLYCAST_CHD:-$REPO/build/flycast/Ikaruga.chd}"
  [[ -f "$chd" ]] || { echo "flycast-alineado: no disc at $chd (copy the CHD, FLYCAST_CHD)" >&2; exit 1; }
  (IFS=,; "$REPO/scripts/capturar-flycast-cuadro.sh" "$chd" --cuadros "${faltan[*]}" --pulsar "$P" --salida "$dir") >&2
fi
for f in "${lista[@]}"; do [[ -s "$f" ]] || { echo "flycast-alineado: missing $f" >&2; exit 1; }; echo "$f"; done
