"""Pure-stdlib PNG -> 8-bit box-art converter used by pack_roms.py (no Pillow in the IDF image).

Output pixels index a fixed 6x6x5 RGB cube: index = r*30 + g*5 + b (r,g in 0..5, b in 0..4),
180 colours, which leaves the top of the palette for the UI. Ordered (Bayer 4x4) dithering
hides most of the banding at box-art size. Non-interlaced 8-bit PNGs only (what
libretro-thumbnails serves); anything else raises and the packer uses a placeholder.
"""
import struct, zlib

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

def decode_png(data):
    """-> (w, h, rows) with rows as lists of (r, g, b)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, plte = 8, [], None
    while pos < len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif typ == b"IDAT":
            idat.append(body)
        pos += 12 + ln
    assert interlace == 0 and (depth == 8 or (depth in (1, 2, 4) and ctype in (0, 3))), f"unsupported PNG (depth {depth}, type {ctype}, interlace {interlace})"
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp if depth == 8 else (w * depth + 7) // 8
    if depth < 8: bpp = 1
    prev = bytearray(stride)
    rows = []
    p = 0
    for _ in range(h):
        f = raw[p]; cur = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: cur[i] = (cur[i] + a) & 255
            elif f == 2: cur[i] = (cur[i] + b) & 255
            elif f == 3: cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if depth < 8:                                   # unpack 1/2/4-bit samples, MSB first
            vals = [(cur[(x * depth) // 8] >> (8 - depth - (x * depth) % 8)) & ((1 << depth) - 1) for x in range(w)]
            if ctype == 3: rows.append([plte[v] for v in vals])
            else: rows.append([(v * 255 // ((1 << depth) - 1),) * 3 for v in vals])
            prev = cur; continue
        if ctype == 2: rows.append([tuple(cur[i:i + 3]) for i in range(0, stride, 3)])
        elif ctype == 6: rows.append([tuple(cur[i:i + 3]) for i in range(0, stride, 4)])
        elif ctype == 0: rows.append([(v, v, v) for v in cur])
        elif ctype == 4: rows.append([(cur[i],) * 3 for i in range(0, stride, 2)])
        elif ctype == 3: rows.append([plte[v] for v in cur])
        prev = cur
    return w, h, rows

def convert(png_bytes, out_w, out_h):
    """Resize (box filter) to fit inside out_w x out_h keeping aspect, then quantise.
    -> (w, h, bytes) of palette indices."""
    w, h, rows = decode_png(png_bytes)
    scale = min(out_w / w, out_h / h)
    ow, oh = max(1, int(w * scale)), max(1, int(h * scale))
    out = bytearray(ow * oh)
    for y in range(oh):
        y0, y1 = y * h // oh, max(y * h // oh + 1, (y + 1) * h // oh)
        for x in range(ow):
            x0, x1 = x * w // ow, max(x * w // ow + 1, (x + 1) * w // ow)
            r = g = b = n = 0
            for yy in range(y0, y1):
                row = rows[yy]
                for xx in range(x0, x1):
                    pr, pg, pb = row[xx]; r += pr; g += pg; b += pb; n += 1
            r, g, b = r / n, g / n, b / n
            d = (BAYER[y & 3][x & 3] + 0.5) / 16 - 0.5     # -0.47 .. +0.47 of one step
            ri = min(5, max(0, int(r / 51 + d + 0.5)))
            gi = min(5, max(0, int(g / 51 + d + 0.5)))
            bi = min(4, max(0, int(b / 63.75 + d + 0.5)))
            out[y * ow + x] = ri * 30 + gi * 5 + bi
    return ow, oh, bytes(out)

def _resample(rows, w, h, ow, oh, x_off=0, y_off=0, src_w=None, src_h=None):
    """box-filter resample of the source window (x_off, y_off, src_w, src_h) to ow x oh -> list of (r,g,b) rows"""
    src_w = src_w or w; src_h = src_h or h
    out = []
    for y in range(oh):
        y0 = y_off + y * src_h // oh; y1 = max(y0 + 1, y_off + (y + 1) * src_h // oh)
        row = []
        for x in range(ow):
            x0 = x_off + x * src_w // ow; x1 = max(x0 + 1, x_off + (x + 1) * src_w // ow)
            r = g = b = n = 0
            for yy in range(y0, y1):
                sr = rows[yy]
                for xx in range(x0, x1):
                    pr, pg, pb = sr[xx]; r += pr; g += pg; b += pb; n += 1
            row.append((r // n, g // n, b // n))
        out.append(row)
    return out

def _median_cut(pixels, count):
    """classic median cut over a list of (r,g,b) -> up to `count` representative colours"""
    boxes = [pixels]
    while len(boxes) < count:
        boxes.sort(key=lambda b: -len(b))
        box = boxes[0]
        if len(box) < 2: break
        rng = [max(p[c] for p in box) - min(p[c] for p in box) for c in range(3)]
        c = rng.index(max(rng))
        if rng[c] == 0: break
        box.sort(key=lambda p: p[c])
        mid = len(box) // 2
        boxes = boxes[1:] + [box[:mid], box[mid:]]
    pal = []
    for b in boxes:
        n = len(b) or 1
        pal.append(tuple(sum(p[c] for p in b) // n for c in range(3)))
    return pal

def rle_rows(rows_idx):
    """mqart-style run-length coding, one row at a time: control < 128 copies control+1 bytes,
    control >= 128 repeats the next byte control-126 times. -> (row_offsets, data)"""
    offs, data = [], bytearray()
    for row in rows_idx:
        offs.append(len(data))
        i, n = 0, len(row)
        while i < n:
            j = i
            while j + 1 < n and row[j + 1] == row[i] and j - i < 128: j += 1
            run = j - i + 1
            if run >= 3:
                data += bytes([126 + run, row[i]]); i += run
            else:
                k = i
                while k < n and k - i < 128 and not (k + 2 < n and row[k] == row[k + 1] == row[k + 2]): k += 1
                data += bytes([k - i - 1]) + bytes(row[i:k]); i = k
    return offs, bytes(data)

def convert_snap(png_bytes, out_w=240, out_h=280, dim=0.30, colours=56):
    """A screenshot as the wheel's backdrop: scaled to fill out_w x out_h (centre-cropped),
    dimmed to `dim`, reduced to its own `colours`-entry palette (median cut, no dither: dim
    pictures dither to noise). -> (palette[(r,g,b)...], row_offsets, rle_bytes), indices 0..colours-1"""
    w, h, rows = decode_png(png_bytes)
    scale = max(out_w / w, out_h / h)
    sw, sh = int(out_w / scale), int(out_h / scale)
    img = _resample(rows, w, h, out_w, out_h, (w - sw) // 2, (h - sh) // 2, sw, sh)
    px = [(int(r * dim), int(g * dim), int(b * dim)) for row in img for (r, g, b) in row]
    sample = px[::7] if len(px) > 20000 else px
    pal = _median_cut(list(sample), colours)
    while len(pal) < colours: pal.append((0, 0, 0))
    def nearest(c):
        return min(range(len(pal)), key=lambda i: (pal[i][0]-c[0])**2 + (pal[i][1]-c[1])**2 + (pal[i][2]-c[2])**2)
    cache = {}
    idx = []
    for c in px:
        k = (c[0] >> 2, c[1] >> 2, c[2] >> 2)
        if k not in cache: cache[k] = nearest(c)
        idx.append(cache[k])
    rows_idx = [idx[y * out_w:(y + 1) * out_w] for y in range(out_h)]
    offs, data = rle_rows(rows_idx)
    return pal, offs, data

def palette_rgb(index):
    """The RGB the firmware assigns to a cube index (keep in sync with main/ui.c)."""
    r, g, b = index // 30, (index // 5) % 6, index % 5
    return r * 51, g * 51, b * 63

if __name__ == "__main__":
    import sys, time
    t = time.time()
    w, h, px = convert(open(sys.argv[1], "rb").read(), 96, 134)
    print(f"{w}x{h}, {len(px)} bytes, {len(set(px))} colours, {time.time()-t:.1f}s")
    # write a PGM-ish preview as PPM for eyeballing
    with open("/tmp/art_preview.ppm", "wb") as f:
        f.write(f"P6 {w} {h} 255\n".encode())
        for v in px: f.write(bytes(palette_rgb(v)))
