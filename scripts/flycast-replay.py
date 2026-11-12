#!/usr/bin/env python3
"""Rewrites the controller input of a RetroArch replay (BSV v2) so the presses land on exact frames.
  flycast-replay.py <base.replay> <out.replay> <presses> [--offset K] [--hold 8]
<base.replay>: recorded with `retroarch -R` and no input. <presses>: "start@700,a@1400" in guest
frames, as the PC launcher's --press; guest frame N is replay frame K+N (K = frames Flycast spends in
its BIOS before the game's frame 0, --offset). Each press is held --hold frames (8, as the PSP).
Buttons are DC names; they go to the RetroPad button Flycast maps to them (a=B, b=A, x=Y, y=X).
Format (RetroArch 1.22 input/bsv/bsvmovie.c): 40-byte header, state block (header word 3 bytes),
then per frame [backref u32][nkeys u8][keys 12 B][nevents u16][events 8 B: port dev idx pad id u16
value i16][next token u8]. Only the joypad mask event (port 0, device 1, id 256) is rewritten."""
import struct, sys

# DC button -> RetroPad bit (RETRO_DEVICE_ID_JOYPAD_*): B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5 LEFT=6 RIGHT=7 A=8 X=9
BIT = {"a": 0, "b": 8, "x": 1, "y": 9, "start": 3, "up": 4, "down": 5, "left": 6, "right": 7}

def main():
    args = sys.argv[1:]
    off, hold, pos = 0, 8, []
    while args:
        a = args.pop(0)
        if a == "--offset": off = int(args.pop(0))
        elif a == "--hold": hold = int(args.pop(0))
        else: pos.append(a)
    if len(pos) != 3: sys.exit(__doc__)
    base, out, presses = pos
    d = bytearray(open(base, "rb").read())
    hdr = struct.unpack("<10I", d[:40])
    assert hdr[0] == 0x42535632 and hdr[1] == 2, "not a BSV v2 replay"
    mask = {}
    for p in filter(None, presses.split(",")):
        b, n = p.split("@")
        for f in range(int(n) + off, int(n) + off + hold):
            mask[f] = mask.get(f, 0) | (1 << BIT[b])
    p, f, patched = 40 + hdr[3], 0, 0
    while p < len(d):
        p += 4
        kc = d[p]; p += 1 + 12 * kc
        ec, = struct.unpack_from("<H", d, p); p += 2
        for i in range(ec):
            port, dev, idx, _, id_, val = struct.unpack_from("<BBBBHh", d, p)
            if (port, dev, idx, id_) == (0, 1, 0, 256):
                struct.pack_into("<h", d, p + 6, mask.get(f, 0)); patched += 1
            p += 8
        tok = d[p]; p += 1
        assert tok == ord("f"), f"unexpected token {tok} at frame {f}"
        f += 1
    assert p == len(d)
    last = max(mask) if mask else 0
    assert patched >= f and last < f, f"replay has {f} frames, {patched} masks, last press {last}"
    open(out, "wb").write(d)
    print(f"flycast-replay: {f} frames, {len(mask)} with a button down")

main()
