#!/usr/bin/env python3
"""Bytes of an ld map's loaded output sections, grouped by where each input section came from.
   scripts/psp/tamano-por-grupo.py build/psp/ikaruga/O2/ikaruga.map"""
import re
import sys
from collections import defaultdict

LOADED = {".init", ".text", ".fini", ".rodata", ".eh_frame", ".gcc_except_table", ".ctors",
          ".dtors", ".data", ".sdata", ".sbss", ".bss", ".init_array", ".fini_array", ".reginfo"}
ALLOC_BSS = {".sbss", ".bss"}


def group(path):
    if "/obj/gen/" in path or path.startswith("obj/gen/"):
        return "generated (gen/*.cpp)"
    if "obj/rt/" in path:
        return "dream-recomp runtime"
    if "obj/host/" in path:
        return "our stubs + main"
    if "libstdc++" in path or "libsupc++" in path:
        return "libstdc++"
    if "libgcc" in path or "crt" in path.split("/")[-1]:
        return "libgcc + crt"
    if re.search(r"/lib(c|m|g|cglue|pthread|pthreadglue)\.a", path):
        return "newlib (libc/libm)"
    if "psp" in path:
        return "pspsdk"
    return "other: " + path


def main(map_path):
    out_sec = None
    started = False
    sizes = defaultdict(lambda: defaultdict(int))
    for line in open(map_path, errors="replace"):
        if not started:
            started = line.startswith("Linker script and memory map")
            continue
        m = re.match(r"^(\.[\w.]+)", line)
        if m:
            out_sec = m.group(1)
            continue
        if out_sec not in LOADED:
            continue
        # Input sections: "[ name] 0xADDR 0xSIZE file". Symbol lines carry one number, fill lines
        # start with *fill*.
        m = re.match(r"^ ?(?:\.\S+|COMMON)?\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m:
            sizes[group(m.group(2).strip())][out_sec] += int(m.group(1), 16)
    total_img = total_bss = 0
    print(f"{'group':28} {'code+ro+data':>14} {'bss':>10}   (bytes)")
    for g, secs in sorted(sizes.items(), key=lambda kv: -sum(kv[1].values())):
        img = sum(v for k, v in secs.items() if k not in ALLOC_BSS)
        bss = sum(v for k, v in secs.items() if k in ALLOC_BSS)
        total_img += img
        total_bss += bss
        print(f"{g:28} {img:14d} {bss:10d}   {(img + bss) / 1048576:7.2f} MB")
    print(f"{'total':28} {total_img:14d} {total_bss:10d}   {(total_img + total_bss) / 1048576:7.2f} MB")
    cols = [".text", ".rodata", ".eh_frame", ".gcc_except_table"]
    print()
    print(f"{'group':28} " + " ".join(f"{c:>18}" for c in cols))
    for g, secs in sorted(sizes.items(), key=lambda kv: -sum(kv[1].values())):
        print(f"{g:28} " + " ".join(f"{secs.get(c, 0):18d}" for c in cols))


if __name__ == "__main__":
    main(sys.argv[1])
