#!/usr/bin/env python3
"""
mkdtdesc.py - build an AmigaOS datatype descriptor (DEVS:DataTypes/xxx).

Host-side replacement for the Amiga 'DTDesc' / AROS 'createdtdesc' tools.
Output is an IFF FORM DTYP with chunks FVER, NAME, DTHD: the mask right
after the header, then the word-aligned strings; string/mask pointers are
stored as offsets from the start of DTHD data. Checked byte-identical to
a descriptor made by the Amiga tool.

  struct DataTypeHeader {          // 32 bytes, big endian
      STRPTR dth_Name, dth_BaseName, dth_Pattern;
      WORD  *dth_Mask;
      ULONG  dth_GroupID, dth_ID;
      WORD   dth_MaskLen, dth_Pad;
      UWORD  dth_Flags, dth_Priority;
  };

Mask syntax: space separated items, each one of
  'X' (a character), ANY (matches any byte, stored 0xFFFF), or a number.
"""
import argparse
import struct
import sys

FLAGS = {
    "DTF_BINARY": 0x0000,
    "DTF_ASCII": 0x0001,
    "DTF_IFF": 0x0002,
    "DTF_MISC": 0x0003,
    "DTF_CASE": 0x0010,
    "DTF_SYSTEM1": 0x1000,
}


def parse_mask(text):
    words = []
    for item in text.split():
        if item == "ANY":
            words.append(0xFFFF)
        elif len(item) == 3 and item[0] == "'" and item[2] == "'":
            words.append(ord(item[1]))
        else:
            words.append(int(item, 0) & 0xFFFF)
    return words


def cstr_even(s):
    b = s.encode("latin-1") + b"\0"
    if len(b) & 1:
        b += b"\0"
    return b


def chunk(cid, data):
    out = cid + struct.pack(">I", len(data)) + data
    if len(data) & 1:
        out += b"\0"
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--name", required=True, help="descriptor name, e.g. WebP")
    ap.add_argument("--basename", required=True,
                    help="class base name: SYS:Classes/DataTypes/<basename>.datatype")
    ap.add_argument("--version", default="", help="FVER string, e.g. '$VER: WebP 45.1 (1.1.2026)'")
    ap.add_argument("--pattern", default="#?")
    ap.add_argument("--mask", default="")
    ap.add_argument("--group", default="pict")
    ap.add_argument("--id", required=True, help="4 character ID, e.g. webp")
    ap.add_argument("--flags", default="DTF_BINARY", help="'|' separated DTF_ flags")
    ap.add_argument("--priority", type=int, default=0)
    a = ap.parse_args()

    if len(a.group) != 4 or len(a.id) != 4:
        sys.exit("group and id must be 4 characters")

    flags = 0
    for f in a.flags.split("|"):
        f = f.strip()
        if f:
            flags |= FLAGS[f]

    mask = parse_mask(a.mask)
    # Commodore's DTDesc keeps each string word aligned (NUL padded).
    name = cstr_even(a.name)
    base = cstr_even(a.basename)
    pat = cstr_even(a.pattern)

    hdrsize = 32
    if mask:
        off_mask = hdrsize
        off_name = hdrsize + 2 * len(mask)
    else:
        off_mask = 0
        off_name = hdrsize
    off_base = off_name + len(name)
    off_pat = off_base + len(base)

    dthd = struct.pack(">IIII4s4shhHH",
                       off_name, off_base, off_pat, off_mask,
                       a.group.encode("ascii"), a.id.encode("ascii"),
                       len(mask), 0, flags, a.priority)
    dthd += b"".join(struct.pack(">H", w) for w in mask)
    dthd += name + base + pat

    body = b"DTYP"
    if a.version:
        body += chunk(b"FVER", a.version.encode("latin-1") + b"\0")
    body += chunk(b"NAME", name)
    body += chunk(b"DTHD", dthd)

    with open(a.output, "wb") as f:
        f.write(b"FORM" + struct.pack(">I", len(body)) + body)


if __name__ == "__main__":
    main()
