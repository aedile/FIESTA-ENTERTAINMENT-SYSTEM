#!/usr/bin/env python3
"""Fetch the menu music: the DuckTales NSF rip from zophar.net -> ROMS/music/menu.nsf.
An NSF is the game's own sound engine and music data with a small player header; nofrendo
plays it through the emulated APU, so no MP3 is involved. Track numbers are in the m3u
inside the zip (7 = Moon Surface)."""
import io, os, sys, urllib.request, zipfile
URL = "https://fi.zophar.net/soundfiles/nintendo-nes-nsf/duck-tales/Duck%20Tales%20%28EMU%29.zophar.zip"
out = sys.argv[1] if len(sys.argv) > 1 else "ROMS/music/menu.nsf"
os.makedirs(os.path.dirname(out), exist_ok=True)
data = urllib.request.urlopen(URL, timeout=60).read()
with zipfile.ZipFile(io.BytesIO(data)) as zf:
    nsf = next(n for n in zf.namelist() if n.lower().endswith(".nsf"))
    open(out, "wb").write(zf.read(nsf))
print(f"wrote {out} from {nsf}")
