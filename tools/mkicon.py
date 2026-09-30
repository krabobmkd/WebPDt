#!/usr/bin/env python3
"""
mkicon.py - write a classic (OS 2.x/3.x, 4 colour) Workbench PROJECT icon.

Usage:
  mkicon.py -o WebP.info --tool C:AddDataTypes [--stack 4096]
            [--tooltype NAME=VALUE ...] [--label TEXT]

Writes a struct DiskObject (do_Magic 0xE310, version 1), one 2-bitplane
Image, the default tool and the tool types; no drawer data (not a drawer),
no position (NO_ICON_POSITION), so Workbench places it itself.
"""
import argparse
import struct

WBPROJECT = 4
NO_ICON_POSITION = 0x80000000

W, H = 36, 22   # icon image size

# 3x5 pixel font, enough for short labels
FONT = {
    "W": ["101", "101", "101", "111", "101"],
    "E": ["111", "100", "110", "100", "111"],
    "B": ["110", "101", "110", "101", "110"],
    "P": ["110", "101", "110", "100", "100"],
    "I": ["111", "010", "010", "010", "111"],
    "N": ["101", "111", "111", "111", "101"],
    "S": ["111", "100", "111", "001", "111"],
    "T": ["111", "010", "010", "010", "010"],
    "D": ["110", "101", "101", "101", "110"],
    " ": ["000", "000", "000", "000", "000"],
}


def draw(label):
    """Pixel colours: 0 grey (bg), 1 black, 2 white, 3 blue (WB 2.x+ palette)."""
    px = [[0] * W for _ in range(H)]
    # sheet of paper with a folded top-right corner
    left, top, right, bottom, fold = 4, 1, 31, 20, 6
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            if x > right - fold and y < top + fold and (x - (right - fold)) > (y - top):
                continue  # cut corner
            px[y][x] = 2
    for x in range(left, right - fold + 1):
        px[top][x] = 1
        px[bottom][x] = 1
    for y in range(top, bottom + 1):
        px[y][left] = 1
    for y in range(top + fold, bottom + 1):
        px[y][right] = 1
    for i in range(fold + 1):
        px[top + i][right - fold + i] = 1          # diagonal
        px[top + fold][right - fold + i] = 1       # fold bottom
        px[top + i][right - fold] = 1              # fold left
    # blue "picture" stripes
    for y in range(12, 18):
        for x in range(left + 3, right - 2):
            if (x + y) % 4 < 2:
                px[y][x] = 3
    # label
    x0 = left + 3
    for ch in label.upper()[:6]:
        g = FONT.get(ch, FONT[" "])
        for gy in range(5):
            for gx in range(3):
                if g[gy][gx] == "1":
                    px[4 + gy][x0 + gx] = 1
        x0 += 4
    return px


def planes(px, depth=2):
    words = (W + 15) // 16
    out = b""
    for p in range(depth):
        for y in range(H):
            row = 0
            for x in range(words * 16):
                bit = (px[y][x] >> p) & 1 if x < W else 0
                row = (row << 1) | bit
            out += row.to_bytes(words * 2, "big")
    return out


def lstr(s):
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--tool", required=True, help="default tool, e.g. C:AddDataTypes")
    ap.add_argument("--stack", type=int, default=4096)
    ap.add_argument("--tooltype", action="append", default=[])
    ap.add_argument("--label", default="WEBP")
    a = ap.parse_args()

    depth = 2
    gadget = struct.pack(">IhhHHHHHIIIIIHI",
                         0,            # NextGadget
                         0, 0,         # LeftEdge, TopEdge
                         W, H + 1,     # Width, Height (+1: WB convention)
                         0x0004,       # Flags: GFLG_GADGIMAGE, GADGHCOMP
                         0x0003,       # Activation: RELVERIFY | GADGIMMEDIATE
                         0x0001,       # GadgetType: BOOLGADGET
                         1,            # GadgetRender (non-NULL: image follows)
                         0,            # SelectRender (none: complement)
                         0, 0, 0,      # GadgetText, MutualExclude, SpecialInfo
                         0,            # GadgetID
                         1)            # UserData: WB_DISKREVISION 1
    assert len(gadget) == 44

    dobj = struct.pack(">HH", 0xE310, 1) + gadget
    dobj += struct.pack(">BBIIIIIII",
                        WBPROJECT, 0,
                        1,                          # do_DefaultTool present
                        1 if a.tooltype else 0,     # do_ToolTypes present
                        NO_ICON_POSITION, NO_ICON_POSITION,
                        0,                          # do_DrawerData
                        0,                          # do_ToolWindow
                        a.stack)
    assert len(dobj) == 78

    image = struct.pack(">hhHHHIBBI", 0, 0, W, H, depth, 1, (1 << depth) - 1, 0, 0)
    assert len(image) == 20

    data = dobj + image + planes(draw(a.label), depth) + lstr(a.tool)
    if a.tooltype:
        data += struct.pack(">I", (len(a.tooltype) + 1) * 4)
        for t in a.tooltype:
            data += lstr(t)

    with open(a.output, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()
