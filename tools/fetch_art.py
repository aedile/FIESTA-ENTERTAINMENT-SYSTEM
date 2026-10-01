#!/usr/bin/env python3
"""Download box art and a gameplay screenshot for every ROM in ROMS/ (and ROMS/disabled/)
from libretro-thumbnails: ROMS/art/<rom name>.png (box) and ROMS/art/snaps/<rom name>.png
(screenshot; the title screen if no gameplay snap exists). Names follow No-Intro, which is
what the ROM files use. Already-present files are skipped. Usage: tools/fetch_art.py [rom_dir]"""
import os, sys, urllib.request, urllib.parse, zipfile

REPO = "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Nintendo_Entertainment_System/master/"
KINDS = [("", ["Named_Boxarts"]), ("snaps", ["Named_Snaps", "Named_Titles"])]

def rom_names(d):
    for fn in sorted(os.listdir(d)):
        p = os.path.join(d, fn)
        if fn.lower().endswith(".nes"):
            yield fn[:-4]
        elif fn.lower().endswith(".zip"):
            with zipfile.ZipFile(p) as zf:
                for n in zf.namelist():
                    if n.lower().endswith(".nes"):
                        yield os.path.basename(n)[:-4]
                        break

rom_dir = sys.argv[1] if len(sys.argv) > 1 else "ROMS"
art_dir = os.path.join(rom_dir, "art")
os.makedirs(art_dir, exist_ok=True)
names = list(rom_names(rom_dir))
if os.path.isdir(os.path.join(rom_dir, "disabled")):
    names += list(rom_names(os.path.join(rom_dir, "disabled")))
import re
def candidates(name):
    """the thumbnail set doesn't carry every No-Intro revision/region tag: try looser names"""
    yield name
    base = re.sub(r" \(Rev [^)]*\)", "", name)
    yield base
    region = re.search(r" \(([^)]*)\)", base)
    stem = base[:region.start()] if region else base
    for r in ("USA", "World", "Japan, USA", "USA, Europe"):
        yield f"{stem} ({r})"

for sub, folders in KINDS:
    os.makedirs(os.path.join(art_dir, sub), exist_ok=True)
    for name in names:
        out = os.path.join(art_dir, sub, name + ".png")
        if os.path.exists(out):
            continue
        for folder in folders:
            for cand in candidates(name):
                url = REPO + folder + "/" + urllib.parse.quote(cand + ".png")
                try:
                    data = urllib.request.urlopen(url, timeout=30).read()
                except Exception:
                    continue
                open(out, "wb").write(data)
                print(f"fetched {sub or 'box'} {name}" + (f" (as {folder}/{cand})" if cand != name or folder != folders[0] else "") + f" ({len(data)//1024} KB)")
                break
            else:
                continue
            break
        else:
            print(f"MISSING {sub or 'box'} {name}: drop a PNG at {out}", file=sys.stderr)
