#!/usr/bin/env bash
# Compares what the PSP EBOOT shows (PPSSPP) with Ikaruga in Flycast, scene by scene.
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
salida="" escenas=() contras=() pos=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --escena) escenas+=("$2"); shift 2 ;;
    --contra) contras+=("$2"); shift 2 ;;
    --salida) salida="$2"; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    -*) echo "unknown argument: $1" >&2; exit 2 ;;
    *) pos+=("$1"); shift ;;
  esac
done
[[ ${#pos[@]} -eq 2 && ${#escenas[@]} -gt 0 ]] || { sed -n '3,4p' "$0" >&2; exit 2; }
OUT="$(realpath -m "${salida:-$REPO/build/flycast/comparacion}")"
mkdir -p "$OUT"

python3 - "$(realpath "${pos[0]}")" "$(realpath "${pos[1]}")" "$OUT" \
  "$(IFS=';'; echo "${escenas[*]}")" "$(IFS=';'; echo "${contras[*]:-}")" <<'PY' | tee "$OUT/resultado.txt"
import math, sys
import numpy as np
from PIL import Image

fly_dir, psp_dir, out, esc_arg, contra_arg = sys.argv[1:]
PSNR_MIN, SSIM_MIN = 20.0, 0.7
REGIONS = {"todo": (0, 480), "izquierda": (0, 100), "centro": (100, 380), "derecha": (380, 480)}

def load(d, i):
    return np.asarray(Image.open(f"{d}/f{i:05d}.png").convert("RGB"), dtype=np.float64)

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

def scores(a, b):
    m = ssim_map(a, b)  # column k of m is centred on pixel column k + 3
    r = {}
    for name, (x0, x1) in REGIONS.items():
        mm = m[:, max(x0 - 3, 0):max(min(x1 - 3, m.shape[1]), 1)]
        r[name] = (psnr(a[:, x0:x1], b[:, x0:x1]), float(mm.mean()))
    return r

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
    Image.fromarray(fly[i].astype(np.uint8)).save(f"{out}/{name}-flycast.png")
    Image.fromarray(psp[j].astype(np.uint8)).save(f"{out}/{name}-psp.png")
    Image.fromarray(np.clip(np.abs(psp[j] - fly[i]) * 4, 0, 255).astype(np.uint8)).save(f"{out}/{name}-dif.png")
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
