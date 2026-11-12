#!/usr/bin/env bash
# Compares what the PSP EBOOT shows (PPSSPP) with Ikaruga in Flycast. Two modes:
#
# (1) One capture against a window of the Flycast reference series (what the image tests use):
#   scripts/comparar-flycast.sh --captura <captura-N.raw|png> (--ventana F0-F1 [--serie DIR] | --imagen FLYCAST.png)
#       [--region NOMBRE:x0,y0,x1,y1]... [--exigir REGLA]... [--iou] [--sin-borde] [--salida DIR]
# The capture is 480x272 (RGB888 .raw as the EBOOT writes it, or a PNG). The Flycast image taken
# is the one of the window [F0,F1] with the highest whole-screen SSIM. Prints PSNR and SSIM for
# todo, izquierda (x 0-99), centro (x 100-379), derecha (x 380-479) and every --region (box
# x0,y0,x1,y1, right/bottom exclusive); --iou adds the IoU of the not-black masks (a pixel is
# geometry when any channel is above 24).
# --exigir REGLA: REGION:psnr>=V, REGION:ssim>=V, iou>=V (or < V); several rules joined with |
# are an OR; every --exigir must hold. Exit 0 when all hold (or none given), 1 when one fails,
# 3 when the scene is not found (window outside the series, or the best match is a window edge
# that is still clearly improving, so the scene is cut by the window), 2 on usage errors.
# --imagen FLYCAST.png: compare against that one 480x272 image instead of a window: the image of the
# same guest frame, from scripts/capturar-flycast-cuadro.sh (p480/f<N>.png). No window search and no
# edge check; the numbers are those of the pair.
# --sin-borde skips the edge check (for negative controls, where the scene is not expected to be
# in the window).
# --serie DIR: default build/flycast/ref/p480; when missing it is recorded with
# scripts/capturar-flycast.sh (the disc: $FLYCAST_CHD, default build/flycast/Ikaruga.chd, copied
# from /tank/JuegosYRoms/dreamcast/Ikaruga.chd when absent; ~2 min). Windows of the scenes the
# tests use: scripts/escenas-flycast.sh.
#
# (2) Series against series, scene by scene (docs/referencia-flycast.md):
#   scripts/comparar-flycast.sh <serie-flycast/p480> <serie-psp> [--escena NOMBRE:F0-F1:P0-P1]...
#                               [--salida DIR] [--contra A:B]...
# <serie-flycast/p480>: 480x272 PNGs from scripts/capturar-flycast.sh. <serie-psp>: 480x272 PNGs
# from probar-ikaruga-ppsspp.sh with PROBAR_GRABAR. Both series are f%05d.png, numbered from 1.
# Alignment is by scene, not by frame: a scene names a window of Flycast frames [F0,F1] and one of
# PSP frames [P0,P1], both picked by eye on the series (the windows only bracket the scene); inside
# them the pair (Flycast i, PSP j) with the highest whole-screen SSIM is taken. The scores of that
# pair are what is reported.
# Metrics per scene: PSNR over RGB and SSIM over luma (BT.601, 7x7 uniform window, K1 0.01,
# K2 0.03, as skimage's default), for the whole screen and for three regions of the 480x272 screen:
# izquierda x 0-99, centro x 100-379, derecha x 380-479 (the game's side columns and play field
# after the 640->480 horizontal scale).
# Thresholds, fixed before measuring: PSNR >= 20 dB and SSIM >= 0.7 on the whole screen.
# --contra A:B compares PSP scene A's chosen frame with Flycast scene B's chosen frame (negative
# control: it must be clearly below the thresholds).
# Output in DIR (default build/flycast/comparacion): resultado.txt, and per scene
# <nombre>-flycast.png, <nombre>-psp.png and <nombre>-dif.png (|difference| x 4).
# Exit 0 when every scene passes and every --contra fails; 1 otherwise; 2 on usage errors.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
salida="" escenas=() contras=() pos=() captura="" ventana="" serie="" imagen="" regiones=() reglas=() iou=0 sinborde=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --escena) escenas+=("$2"); shift 2 ;;
    --contra) contras+=("$2"); shift 2 ;;
    --salida) salida="$2"; shift 2 ;;
    --captura) captura="$2"; shift 2 ;;
    --ventana) ventana="$2"; shift 2 ;;
    --serie) serie="$2"; shift 2 ;;
    --imagen) imagen="$2"; shift 2 ;;
    --region) regiones+=("$2"); shift 2 ;;
    --exigir) reglas+=("$2"); shift 2 ;;
    --iou) iou=1; shift ;;
    --sin-borde) sinborde=1; shift ;;
    -h|--help) sed -n '2,38p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done

if [[ -n "$captura" ]]; then
  if [[ -n "$imagen" ]]; then
    [[ -f "$captura" && -f "$imagen" && -z "$ventana" && -z "$serie" ]] || { sed -n '4,6p' "$0" >&2; exit 2; }
    serie="$(mktemp -d)"; trap 'rm -rf "$serie"' EXIT
    cp "$imagen" "$serie/f00001.png"; ventana="1-1"; sinborde=1
  else
    [[ -f "$captura" && "$ventana" =~ ^[0-9]+-[0-9]+$ ]] || { sed -n '4,6p' "$0" >&2; exit 2; }
  fi
  serie="${serie:-$REPO/build/flycast/ref/p480}"
  if [[ ! -s "$serie/f00001.png" ]]; then
    chd="${FLYCAST_CHD:-$REPO/build/flycast/Ikaruga.chd}"
    if [[ ! -f "$chd" ]]; then
      mkdir -p "$(dirname "$chd")"
      cp /tank/JuegosYRoms/dreamcast/Ikaruga.chd "$chd" ||
        { echo "comparar-flycast: no disc: set FLYCAST_CHD" >&2; exit 2; }
    fi
    echo "comparar-flycast: recording the Flycast reference series into $(dirname "$serie")" >&2
    "$REPO/scripts/capturar-flycast.sh" "$chd" --segundos 60 --salida "$(dirname "$serie")" >&2
  fi
  OUT="$(realpath -m "${salida:-$REPO/build/flycast/comparacion}")"
  mkdir -p "$OUT"
  MODO=captura
  ARG1="$(realpath "$captura")"; ARG2="$(realpath "$serie")"; ARG3="$ventana"
  ARG4="$(IFS=';'; echo "${regiones[*]:-}")"; ARG5="$(IFS=';'; echo "${reglas[*]:-}")"
  ARG6="$iou$sinborde"
else
  [[ ${#pos[@]} -eq 2 && ${#escenas[@]} -gt 0 ]] || { sed -n '4,6p;24,26p' "$0" >&2; exit 2; }
  OUT="$(realpath -m "${salida:-$REPO/build/flycast/comparacion}")"
  mkdir -p "$OUT"
  MODO=series
  ARG1="$(realpath "${pos[0]}")"; ARG2="$(realpath "${pos[1]}")"
  ARG3="$(IFS=';'; echo "${escenas[*]}")"; ARG4="$(IFS=';'; echo "${contras[*]:-}")"; ARG5=""; ARG6=00
fi

python3 - "$MODO" "$OUT" "$ARG1" "$ARG2" "$ARG3" "$ARG4" "$ARG5" "$ARG6" <<'PY' | tee "$OUT/resultado.txt"
import math, os, re, sys
import numpy as np
from PIL import Image

modo, out, arg1, arg2, arg3, arg4, arg5, arg6 = sys.argv[1:]
PSNR_MIN, SSIM_MIN = 20.0, 0.7
REGIONS = {"todo": (0, 0, 480, 272), "izquierda": (0, 0, 100, 272),
           "centro": (100, 0, 380, 272), "derecha": (380, 0, 480, 272)}

def load(d, i):
    return np.asarray(Image.open(f"{d}/f{i:05d}.png").convert("RGB"), dtype=np.float64)

def load_capture(path):
    if path.endswith(".raw"):
        data = open(path, "rb").read()
        if len(data) != 480 * 272 * 3:
            sys.exit(f"comparar-flycast: {path} is not 480x272 RGB888 ({len(data)} bytes)")
        im = Image.frombytes("RGB", (480, 272), data)
    else:
        im = Image.open(path).convert("RGB")
        if im.size != (480, 272):
            sys.exit(f"comparar-flycast: {path} is {im.size}, not 480x272")
    return np.asarray(im, dtype=np.float64)

def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114

def box(x, w=7):
    # Mean over every w x w window (valid part only), via an integral image.
    s = np.pad(x, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    return (s[w:, w:] - s[:-w, w:] - s[w:, :-w] + s[:-w, :-w]) / (w * w)

def ssim_map(a, b):
    x, y = luma(a), luma(b)
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    n = 49
    mx, my = box(x), box(y)
    vx = (box(x * x) - mx * mx) * n / (n - 1)
    vy = (box(y * y) - my * my) * n / (n - 1)
    cxy = (box(x * y) - mx * my) * n / (n - 1)
    return ((2 * mx * my + c1) * (2 * cxy + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))

def psnr(a, b):
    mse = np.mean((a - b) ** 2)
    return float("inf") if mse == 0 else 10 * math.log10(255 * 255 / mse)

def scores(a, b, regions=REGIONS):
    m = ssim_map(a, b)  # cell (r, k) of m is centred on pixel (r + 3, k + 3)
    r = {}
    for name, (x0, y0, x1, y1) in regions.items():
        mm = m[max(y0 - 3, 0):max(min(y1 - 3, m.shape[0]), 1), max(x0 - 3, 0):max(min(x1 - 3, m.shape[1]), 1)]
        r[name] = (psnr(a[y0:y1, x0:x1], b[y0:y1, x0:x1]), float(mm.mean()))
    return r

def mask(a):
    return a.max(axis=2) > 24  # not black: any channel above 24

def iou(a, b):
    ma, mb = mask(a), mask(b)
    union = np.logical_or(ma, mb).sum()
    return float(np.logical_and(ma, mb).sum() / union) if union else 0.0

def mkdiff(a, b):
    return np.clip(np.abs(a - b) * 4, 0, 255).astype(np.uint8)

def save(path, a):
    Image.fromarray(np.asarray(a).astype(np.uint8)).save(path)

if modo == "captura":
    cap_path, serie, ventana, regs_arg, rules_arg, want_iou = arg1, arg2, arg3, arg4, arg5, arg6[0] == "1"
    f0, f1 = map(int, ventana.split("-"))
    regions = dict(REGIONS)
    for r in filter(None, regs_arg.split(";")):
        name, box_s = r.split(":")
        regions[name] = tuple(int(v) for v in box_s.split(","))
    n_series = len([n for n in os.listdir(serie) if re.fullmatch(r"f\d{5}\.png", n)])
    if f0 < 1 or f1 > n_series or f0 > f1:
        print(f"ESCENA NO ENCONTRADA: ventana {f0}-{f1} fuera de la serie (1-{n_series})")
        sys.exit(3)
    cap = load_capture(cap_path)
    sc = {i: float(ssim_map(cap, load(serie, i)).mean()) for i in range(f0, f1 + 1)}
    best = max(sc, key=sc.get)
    # The best match on a window edge that is still improving inward-to-outward: the scene
    # continues outside the window (Flycast runs by wall clock, so the series can shift).
    for edge, other in ((f0, f0 + 3), (f1, f1 - 3)):
        if not arg6[1] == "1" and best == edge and f1 - f0 >= 6 and sc[edge] - sc[other] > 0.02:
            print(f"ESCENA NO ENCONTRADA: la mejor imagen (f{best:05d}, ssim {sc[best]:.3f}) es el borde "
                  f"de la ventana {f0}-{f1} y sigue mejorando hacia fuera (f{other:05d}: {sc[other]:.3f})")
            sys.exit(3)
    fly = load(serie, best)
    r = scores(cap, fly, regions)
    r_iou = iou(cap, fly)
    name = os.path.splitext(os.path.basename(cap_path))[0]
    save(f"{out}/{name}-psp.png", cap)
    save(f"{out}/{name}-flycast.png", fly)
    save(f"{out}/{name}-dif.png", mkdiff(cap, fly))
    print(f"captura {os.path.basename(cap_path)} vs flycast f{best:05d} (ventana {f0}-{f1}, serie de {n_series})")
    for reg, (pp, ss) in r.items():
        print(f"  {reg:9s} psnr {pp:6.2f} dB  ssim {ss:.3f}")
    if want_iou:
        print(f"  iou {r_iou:.4f}  (geometria psp {mask(cap).mean():.4f} flycast {mask(fly).mean():.4f} de la pantalla)")
    ok = True
    for rule in filter(None, rules_arg.split(";")):
        alts = []
        for alt in rule.split("|"):
            m = re.fullmatch(r"(?:(\w+):)?(psnr|ssim|iou)(>=|<)([0-9.]+)", alt.strip())
            if not m:
                sys.exit(f"comparar-flycast: bad rule {alt!r}")
            reg, metric, op, val = m.groups()
            if metric == "iou":
                got = r_iou
            else:
                if reg not in r:
                    sys.exit(f"comparar-flycast: unknown region {reg!r}")
                got = r[reg][0 if metric == "psnr" else 1]
            alts.append((alt.strip(), got, (got >= float(val)) if op == ">=" else (got < float(val))))
        passed = any(a[2] for a in alts)
        ok &= passed
        print(f"  {'ok  ' if passed else 'FAIL'} {rule}  ->  " + ", ".join(f"{a[1]:.3f}" for a in alts))
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)

fly_dir, psp_dir, esc_arg, contra_arg = arg1, arg2, arg3, arg4
escenas = []
for e in esc_arg.split(";"):
    name, f, p = e.split(":")
    f0, f1 = map(int, f.split("-"))
    p0, p1 = map(int, p.split("-"))
    escenas.append((name, f0, f1, p0, p1))

ok = True
elegidos = {}
for name, f0, f1, p0, p1 in escenas:
    fly = {i: load(fly_dir, i) for i in range(f0, f1 + 1)}
    psp = {j: load(psp_dir, j) for j in range(p0, p1 + 1)}
    best = None
    for i, a in fly.items():
        for j, b in psp.items():
            s = float(ssim_map(b, a).mean())
            if best is None or s > best[0]:
                best = (s, i, j)
    _, i, j = best
    elegidos[name] = (fly[i], psp[j], i, j)
    r = scores(psp[j], fly[i])
    save(f"{out}/{name}-flycast.png", fly[i])
    save(f"{out}/{name}-psp.png", psp[j])
    save(f"{out}/{name}-dif.png", mkdiff(psp[j], fly[i]))
    p, s = r["todo"]
    pasa = p >= PSNR_MIN and s >= SSIM_MIN
    ok &= pasa
    print(f"escena {name}: flycast f{i:05d} (ventana {f0}-{f1}), psp f{j:05d} (ventana {p0}-{p1})")
    for reg, (pp, ss) in r.items():
        print(f"  {reg:9s} psnr {pp:6.2f} dB  ssim {ss:.3f}")
    print(f"  {'PASS' if pasa else 'FAIL'} (umbral psnr >= {PSNR_MIN:g} dB y ssim >= {SSIM_MIN:g})")

for c in filter(None, contra_arg.split(";")):
    a, b = c.split(":")
    psp_img, fly_img = elegidos[a][1], elegidos[b][0]
    p, s = scores(psp_img, fly_img)["todo"]
    bajo = p < PSNR_MIN or s < SSIM_MIN
    ok &= bajo
    print(f"contra psp {a} vs flycast {b}: psnr {p:.2f} dB ssim {s:.3f} -> "
          f"{'ok (bajo el umbral)' if bajo else 'FAIL (pasa el umbral)'}")
sys.exit(0 if ok else 1)
PY
