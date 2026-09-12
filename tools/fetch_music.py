#!/usr/bin/env python3
"""Fetch the menu music: the DuckTales NSF from the joshw NSF archive -> ROMS/music/menu.nsf.
An NSF is the game's own sound engine and music data with a small player header (this rip's
banks are byte-identical to the US ROM's fixed bank); nofrendo plays it through the emulated
APU, so no MP3 is involved. Track 7 = The Moon (see the m3u in the archive).
Needs bsdtar (ships with macOS) to unpack the 7z."""
import os, subprocess, sys, tempfile, urllib.request
URL = "https://nsf.joshw.info/d/DuckTales%20%5BWanpaku%20Duck%20Yume%20Bouken%5D%20(1989-09)(Capcom)%5BNES%5D.7z"
out = sys.argv[1] if len(sys.argv) > 1 else "ROMS/music/menu.nsf"
os.makedirs(os.path.dirname(out), exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    arc = os.path.join(tmp, "dt.7z")
    open(arc, "wb").write(urllib.request.urlopen(URL, timeout=60).read())
    subprocess.check_call(["bsdtar", "-xf", arc, "-C", tmp])
    nsf = next(os.path.join(r, f) for r, _, fs in os.walk(tmp) for f in fs if f.lower().endswith(".nsf"))
    open(out, "wb").write(open(nsf, "rb").read())
    print(f"wrote {out} from {os.path.basename(nsf)}")
