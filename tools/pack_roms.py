#!/usr/bin/env python3
"""Pack every .nes (or zipped .nes) in a folder, plus its box art, into one flash image for the
'roms' partition.

Layout (little-endian):
    "NESR"  u32 count
    count x { char name[48]; u32 offset; u32 size; u32 art_offset; u16 art_w; u16 art_h; u32 snap_offset; }   (68 bytes)
    the ROM files (16-byte aligned, iNES header kept), then the art bitmaps, then the snaps.
Art: <rom_dir>/art/<rom name>.png converted by artconv.py to 8-bit indices into a 6x6x5 RGB
cube, at most ART_W x ART_H. art_offset 0 = no art.
Snap: <rom_dir>/art/snaps/<rom name>.png as the wheel's backdrop: 240x280, dimmed, in its own
56-colour palette, run-length coded per row (see artconv.rle_rows). Blob: 56 x (r,g,b),
u32 row_offset[280] (from the start of the row data), then the rows. snap_offset 0 = none.

Usage: pack_roms.py <rom_dir> <out.bin>
"""
import os, struct, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import artconv

MAX_NAME = 48
ENTRY = "<48sIIIHHI"
SNAP_W, SNAP_H, SNAP_COLOURS = 240, 280, 56
ART_W, ART_H = 96, 134

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

def load_art(rom_dir, name):
    p = os.path.join(rom_dir, "art", name + ".png")
    if not os.path.exists(p):
        return 0, 0, b""
    try:
        return artconv.convert(open(p, "rb").read(), ART_W, ART_H)
    except Exception as e:
        print(f"pack_roms: art for {name} unusable ({e}), skipped", file=sys.stderr)
        return 0, 0, b""

def load_snap(rom_dir, name):
    p = os.path.join(rom_dir, "art", "snaps", name + ".png")
    if not os.path.exists(p):
        return b""
    try:
        pal, offs, data = artconv.convert_snap(open(p, "rb").read(), SNAP_W, SNAP_H, colours=SNAP_COLOURS)
    except Exception as e:
        print(f"pack_roms: snap for {name} unusable ({e}), skipped", file=sys.stderr)
        return b""
    return b"".join(bytes(c) for c in pal) + struct.pack(f"<{SNAP_H}I", *offs) + data

def align(n): return -n & 15

def pack(roms, arts, snaps):
    hdr = struct.calcsize("<4sI") + len(roms) * struct.calcsize(ENTRY)
    off = hdr + align(hdr)
    blob = b""
    def put(chunk):
        nonlocal off, blob
        at = off if chunk else 0
        blob += chunk + b"\xff" * align(len(chunk))
        off += len(chunk) + align(len(chunk))
        return at
    rom_offs = [put(data) for _, data in roms]
    art_offs = [put(px) for _, _, px in arts]
    snap_offs = [put(sn) for sn in snaps]
    table = b"".join(struct.pack(ENTRY, name.encode("utf8")[:MAX_NAME - 1], ro, len(data), ao, w, h, so)
                     for (name, data), ro, ao, (w, h, _), so in zip(roms, rom_offs, art_offs, arts, snap_offs))
    return struct.pack("<4sI", b"NESR", len(roms)) + table + b"\xff" * align(hdr) + blob

if __name__ == "__main__":
    roms = collect(sys.argv[1])
    arts = [load_art(sys.argv[1], n) for n, _ in roms]
    snaps = [load_snap(sys.argv[1], n) for n, _ in roms]
    img = pack(roms, arts, snaps)
    with open(sys.argv[2], "wb") as f:
        f.write(img)
    for (name, data), (w, h, px), sn in zip(roms, arts, snaps):
        hd = data[:16]
        print(f"pack_roms: {name:44s} {len(data)//1024:5d}K mapper {(hd[6] >> 4) | (hd[7] & 0xF0):3d}"
              f"{' batt' if hd[6] & 2 else '     '}  art {f'{w}x{h}' if px else 'none'}  snap {f'{len(sn)//1024}K' if sn else 'none'}")
    print(f"pack_roms: {len(roms)} ROMs, {len(img)} bytes -> {sys.argv[2]}")
