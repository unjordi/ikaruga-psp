#!/bin/bash
# armar-docs.sh [--salida archivo.pdf] [--con-mediciones]
# Builds one PDF (with table of contents) from the docs listed in docs/README.md, in the order of
# its tables. The index is the only source of the list: a doc added to the index appears in the PDF.
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SALIDA="$RAIZ/build/docs/ikaruga-psp.pdf"
CON_MEDICIONES=0

while [ $# -gt 0 ]; do
  case "$1" in
    --salida) [ $# -ge 2 ] || { echo "[armar-docs] --salida needs a file" >&2; exit 2; }; SALIDA="$2"; shift 2 ;;
    --con-mediciones) CON_MEDICIONES=1; shift ;;
    -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "[armar-docs] unknown option: $1" >&2; exit 2 ;;
  esac
done

faltan=()
command -v pandoc >/dev/null || faltan+=("pandoc (pacman -S pandoc-cli, or pandoc-bin from the AUR)")
MOTOR="${ARMAR_DOCS_MOTOR:-xelatex}"
if ! command -v "$MOTOR" >/dev/null || { [ "$MOTOR" = xelatex ] && [ -z "$(kpsewhich -engine=xetex -format=fmt xelatex.fmt 2>/dev/null)" ]; }; then
  faltan+=("$MOTOR (pacman -S texlive-xetex texlive-latexextra)")
fi
[ ${#faltan[@]} -eq 0 ] || { printf '[armar-docs] missing: %s\n' "${faltan[@]}" >&2; exit 1; }
for f in "DejaVu Sans" "DejaVu Sans Mono"; do
  fc-list "$f" | grep -q . || { echo "[armar-docs] missing font '$f' (pacman -S ttf-dejavu)" >&2; exit 1; }
done

INDICE="$RAIZ/docs/README.md"
[ -f "$INDICE" ] || { echo "[armar-docs] $INDICE not found" >&2; exit 1; }

# Paths (relative to docs/) of every table row of the index that links a .md, in order.
mapfile -t TODOS < <(perl -ne 'print "$1\n" if /^\|\s*\[[^\]]*\]\(([^)#]+\.md)\)/' "$INDICE")
DOCS=()
for d in "${TODOS[@]}"; do
  case "$d" in mediciones/*) continue ;; esac
  DOCS+=("$d")
done
if [ "$CON_MEDICIONES" = 1 ]; then
  for d in "${TODOS[@]}"; do
    case "$d" in mediciones/*) DOCS+=("$d") ;; esac
  done
fi
[ ${#DOCS[@]} -gt 0 ] || { echo "[armar-docs] no docs found in the tables of $INDICE" >&2; exit 1; }
for d in "${DOCS[@]}"; do
  [ -f "$RAIZ/docs/$d" ] || { echo "[armar-docs] $INDICE lists $d but docs/$d does not exist" >&2; exit 1; }
done

TMP="$(mktemp -d "${TMPDIR:-/tmp}/armar-docs.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$(dirname "$SALIDA")"

# Rewrites the relative links between included docs to internal anchors; any other relative link
# becomes plain text. Adds a page break and the anchor of each doc.
cat > "$TMP/enlaces.pl" <<'PERL'
use strict; use warnings; use File::Basename; use File::Spec;
my ($actual, @incluidos) = @ARGV;
my %id = map { my $k = $_; (my $v = $k) =~ s/\.md$//; $v =~ s{[/_.]}{-}g; ($k => "doc-$v") } @incluidos;
my $dir = dirname($actual);
local $/; my $t = <STDIN>;
my @partes = split /(```.*?```)/s, $t;   # fenced code is left as is
for my $p (@partes) {
  next if $p =~ /^```/;
  $p =~ s{(?<!!)\[([^\]]*)\]\(([^)\s]*)\)}{
    my ($txt, $dst) = ($1, $2);
    if ($dst =~ m{^(?:[a-z][a-z0-9+.-]*:|#)}i) { "[$txt]($dst)" }
    else {
      (my $ruta = $dst) =~ s/#.*//;
      my $abs = File::Spec->canonpath(File::Spec->catfile($dir, $ruta));
      1 while $abs =~ s{[^/]+/\.\./}{};
      $abs =~ s{^\./}{};
      exists $id{$abs} ? "[$txt](#$id{$abs})" : $txt;
    }
  }ge;
}
print "\\newpage\n\n[]{#$id{$actual}}\n\n", join("", @partes), "\n";
PERL

# Pandoc filter: code blocks without a language go through the highlighter (which breaks long
# lines); Japanese runs use the CJK font; emoji without an outline glyph become DejaVu symbols.
cat > "$TMP/codigo.lua" <<'LUA'
local mapa = { ["\u{2705}"] = "\u{2714}", ["\u{2B1C}"] = "\u{2610}", ["\u{1F504}"] = "\u{21BB}" }
local function es_cjk(cp) return (cp >= 0x3000 and cp <= 0x30FF) or (cp >= 0x4E00 and cp <= 0x9FFF) or (cp >= 0xFF00 and cp <= 0xFFEF) end

-- Splits s into {texto, cjk} runs.
local function partir(s)
  local runs, cur, cur_cjk = {}, {}, false
  for _, cp in utf8.codes(s) do
    local ch = utf8.char(cp)
    ch = mapa[ch] or ch
    local c = es_cjk(cp)
    if c ~= cur_cjk and #cur > 0 then
      runs[#runs + 1] = { table.concat(cur), cur_cjk }
      cur = {}
    end
    cur_cjk = c
    cur[#cur + 1] = ch
  end
  if #cur > 0 then runs[#runs + 1] = { table.concat(cur), cur_cjk } end
  return runs
end

local function con_cjk(s)
  for _, cp in utf8.codes(s) do if es_cjk(cp) then return true end end
  return false
end

local function por_tramos(s, hacer_texto)
  local out = {}
  for _, r in ipairs(partir(s)) do
    if r[2] then out[#out + 1] = pandoc.RawInline("latex", "{\\cjkfont " .. r[1] .. "}")
    else out[#out + 1] = hacer_texto(r[1]) end
  end
  return out
end

function Str(e) return por_tramos(e.text, pandoc.Str) end
function Code(e)
  return por_tramos(e.text, function(t) return pandoc.Code(t, e.attr) end)
end

-- Three ASCII characters absent from the block, to use as Verbatim's escape/open/close.
local function libres(text)
  local out = {}
  for ch in ("@!?~^;"):gmatch(".") do
    if not text:find(ch, 1, true) then out[#out + 1] = ch end
  end
  if #out < 3 then error("[armar-docs] no free characters to mark Japanese text in a code block") end
  return out[1], out[2], out[3]
end

function CodeBlock(b)
  if con_cjk(b.text) then
    local e, o, c = libres(b.text)
    local lineas = {}
    for _, r in ipairs(partir(b.text)) do
      lineas[#lineas + 1] = r[2] and (e .. "cjkfont" .. o .. r[1] .. c) or r[1]
    end
    return pandoc.RawBlock("latex", "\\begin{Verbatim}[breaklines,commandchars=" .. e .. o .. c .. "]\n" .. table.concat(lineas) .. "\n\\end{Verbatim}")
  end
  if #b.classes == 0 then b.classes = { "text" } end
  for k, v in pairs(mapa) do b.text = b.text:gsub(k, v) end
  return b
end
LUA

cat > "$TMP/cabecera.tex" <<'TEX'
\usepackage{etoolbox}
\usepackage{fvextra}
\newfontfamily\cjkfont{Noto Sans CJK JP}
\usepackage{xurl}
\AtBeginEnvironment{longtable}{\footnotesize}
\setlength{\emergencystretch}{3em}
\sloppy
\fvset{breaklines=true,breakanywhere=true}
\renewcommand{\arraystretch}{1.15}
TEX

ENTRADAS=()
for d in "${DOCS[@]}"; do
  out="$TMP/$(echo "$d" | tr '/' '_')"
  perl "$TMP/enlaces.pl" "$d" "${DOCS[@]}" < "$RAIZ/docs/$d" > "$out"
  ENTRADAS+=("$out")
done

FECHA="$(git -C "$RAIZ" log -1 --format=%cs 2>/dev/null || date +%F)"
COMMIT="$(git -C "$RAIZ" rev-parse --short HEAD 2>/dev/null || echo "?")"

# Run from docs/ so that images (if any) resolve relative to it.
( cd "$RAIZ/docs" && pandoc "${ENTRADAS[@]}" \
    --from markdown+pipe_tables+backtick_code_blocks \
    --pdf-engine="$MOTOR" \
    --lua-filter="$TMP/codigo.lua" \
    --include-in-header="$TMP/cabecera.tex" \
    --toc --toc-depth=2 \
    --syntax-highlighting=tango \
    -V lang=es -V documentclass=report -V papersize=a4 -V geometry:margin=2cm \
    -V fontsize=10pt -V linkcolor=blue -V urlcolor=blue -V colorlinks=true \
    -V mainfont="DejaVu Sans" -V monofont="DejaVu Sans Mono" -V monofontoptions=Scale=0.85 \
    -M title="ikaruga-psp" -M subtitle="Plan y documentación" -M date="$FECHA ($COMMIT)" \
    -o "$SALIDA" ) 2> "$TMP/pandoc.log" || { cat "$TMP/pandoc.log" >&2; echo "[armar-docs] pandoc failed" >&2; exit 1; }
cat "$TMP/pandoc.log" >&2

echo "[armar-docs] ${#DOCS[@]} docs -> $SALIDA"
