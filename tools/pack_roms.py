#!/usr/bin/env python3
"""Pack every .nes (or zipped .nes) in a folder into one flash image for the 'roms' partition.

Layout (little-endian):
    "NESR"  u32 count  then count x { char name[48]; u32 offset; u32 size; }  then the files.
Files are 16-byte aligned and keep their iNES header. Offsets are from the start of the image.

Usage: pack_roms.py <rom_dir> <out.bin>
"""
import os, struct, sys, zipfile

MAX_NAME = 48

def collect(rom_dir):
    roms = []
    for fn in sorted(os.listdir(rom_dir), key=str.lower):
        path = os.path.join(rom_dir, fn)
        if fn.lower().endswith(".nes"):
            roms.append((fn[:-4], open(path, "rb").read()))
        elif fn.lower().endswith(".zip"):
            with zipfile.ZipFile(path) as zf:
                for n in zf.namelist():
                    if n.lower().endswith(".nes"):
                        roms.append((os.path.basename(n)[:-4], zf.read(n)))
                        break
                else:
                    print(f"pack_roms: {fn}: no .nes inside, skipped", file=sys.stderr)
    return [(n, d) for n, d in roms if d[:4] == b"NES\x1a" or print(f"pack_roms: {n}: not an iNES file, skipped", file=sys.stderr)]

def pack(roms):
    hdr = struct.calcsize("<4sI") + len(roms) * (MAX_NAME + 8)
    off = (hdr + 15) & ~15
    table, blob = b"", b""
    for name, data in roms:
        table += struct.pack(f"<{MAX_NAME}sII", name.encode("utf8")[:MAX_NAME - 1], off, len(data))
        blob += data + b"\xff" * (-len(data) & 15)
        off += len(data) + (-len(data) & 15)
    head = struct.pack("<4sI", b"NESR", len(roms)) + table
    return head + b"\xff" * (-hdr & 15) + blob

if __name__ == "__main__":
    roms = collect(sys.argv[1])
    img = pack(roms)
    with open(sys.argv[2], "wb") as f:
        f.write(img)
    for name, data in roms:
        h = data[:16]
        print(f"pack_roms: {name:44s} {len(data)//1024:5d}K mapper {(h[6] >> 4) | (h[7] & 0xF0):3d}{' batt' if h[6] & 2 else ''}")
    print(f"pack_roms: {len(roms)} ROMs, {len(img)} bytes -> {sys.argv[2]}")
