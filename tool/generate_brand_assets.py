#!/usr/bin/env python3
"""Build native FORTIQ icons from the checked-in isolated PNG mark.
Python standard library only. No Pillow, ImageMagick or internet needed.
"""
import argparse
import binascii
import math
import struct
import zlib
from collections import deque
from pathlib import Path

SIG = b'\x89PNG\r\n\x1a\n'


def decode(path):
    raw = path.read_bytes()
    if not raw.startswith(SIG):
        raise ValueError("Expected PNG: " + str(path))
    off, palette, trans, compressed = 8, b'', b'', bytearray()
    width = height = bitdepth = colortype = interlace = None
    while off < len(raw):
        n = struct.unpack_from(">I", raw, off)[0]
        typ = raw[off + 4:off + 8]
        content = raw[off + 8:off + 8 + n]
        crc = struct.unpack_from(">I", raw, off + 8 + n)[0]
        if binascii.crc32(typ + content) & 0xffffffff != crc:
            raise ValueError("PNG CRC error")
        off += n + 12
        if typ == b'IHDR':
            width, height, bitdepth, colortype, _, _, interlace = struct.unpack(">IIBBBBB", content)
        elif typ == b'PLTE':
            palette = content
        elif typ == b'tRNS':
            trans = content
        elif typ == b'IDAT':
            compressed.extend(content)
        elif typ == b'IEND':
            break
    if bitdepth != 8 or interlace or colortype not in (0, 2, 3, 4, 6):
        raise ValueError("Expected 8-bit non-interlaced grayscale/RGB/indexed/RGBA PNG")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype]
    stride = width * bpp
    data = memoryview(zlib.decompress(compressed))
    out = bytearray(width * height * 4)
    pos = 0
    prev = bytearray(stride)
    for y in range(height):
        filt = data[pos]
        pos += 1
        row = bytearray(data[pos:pos + stride])
        pos += stride
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if filt == 0:
                v = 0
            elif filt == 1:
                v = a
            elif filt == 2:
                v = b
            elif filt == 3:
                v = (a + b) // 2
            elif filt == 4:
                p = a + b - c
                da, db, dc = abs(p - a), abs(p - b), abs(p - c)
                v = a if da <= db and da <= dc else (b if db <= dc else c)
            else:
                raise ValueError("Invalid PNG filter")
            row[i] = (row[i] + v) & 255
        prev = row
        for x in range(width):
            i, j = x * bpp, (y * width + x) * 4
            if colortype == 6:
                rgba = row[i:i + 4]
            elif colortype == 2:
                rgba = (*row[i:i + 3], 255)
            elif colortype == 0:
                rgba = (row[i], row[i], row[i], 255)
            elif colortype == 4:
                rgba = (row[i], row[i], row[i], row[i + 1])
            else:
                k = row[i]
                rgba = (*palette[3 * k:3 * k + 3], trans[k] if k < len(trans) else 255)
            out[j:j + 4] = bytes(rgba)
    return width, height, out


def png(w, h, pixels):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", binascii.crc32(tag + data) & 0xffffffff)
    scan = b''.join(b'\x00' + pixels[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return SIG + chunk(b'IHDR', struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan, 9)) + chunk(b'IEND', b'')


def crop_bounds(w, h, px):
    corners = [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]
    if all(px[(y * w + x) * 4 + 3] >= 240 for x, y in corners):
        samples = [px[(y * w + x) * 4:(y * w + x) * 4 + 3] for x, y in corners]
        base = samples[0]
        if max(base) > 115 or max(abs(base[k] - s[k]) for s in samples for k in range(3)) > 32:
            raise ValueError("Opaque illustrated crop: use isolated assets/branding/fortiq_mark.png")
        seen = bytearray(w * h)
        queue = deque([x for x in range(w)] + [(h - 1) * w + x for x in range(w)]
                      + [y * w for y in range(h)] + [y * w + w - 1 for y in range(h)])
        while queue:
            k = queue.popleft()
            if seen[k]:
                continue
            seen[k] = 1
            a = 4 * k
            if sum((px[a + i] - base[i]) ** 2 for i in range(3)) > 47 ** 2:
                continue
            px[a + 3] = 0
            y, x = divmod(k, w)
            if x:
                queue.append(k - 1)
            if x < w - 1:
                queue.append(k + 1)
            if y:
                queue.append(k - w)
            if y < h - 1:
                queue.append(k + w)
    xs = []
    ys = []
    for y in range(h):
        for x in range(w):
            if px[(y * w + x) * 4 + 3] >= 25:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise ValueError("PNG mark has no visible pixels")
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def sample(src, sw, sh, u, v):
    if u < 0 or v < 0 or u > sw - 1 or v > sh - 1:
        return (0, 0, 0, 0)
    x0, y0 = int(u), int(v)
    x1, y1 = min(sw - 1, x0 + 1), min(sh - 1, y0 + 1)
    dx, dy = u - x0, v - y0
    locations = ((y0 * sw + x0) * 4, (y0 * sw + x1) * 4,
                 (y1 * sw + x0) * 4, (y1 * sw + x1) * 4)
    weights = ((1 - dx) * (1 - dy), dx * (1 - dy), (1 - dx) * dy, dx * dy)
    pts = [src[k:k + 4] for k in locations]
    alpha = sum(pt[3] * f for pt, f in zip(pts, weights))
    if alpha < .1:
        return (0, 0, 0, 0)
    rgb = [round(sum(pt[c] * pt[3] * f for pt, f in zip(pts, weights)) / alpha) for c in range(3)]
    return (*rgb, round(alpha))


def render(size, src, sw, sh, box, tile=False):
    result = bytearray(size * size * 4)
    x0, y0, x1, y1 = box
    scale = size * (.69 if tile else .63) / max(x1 - x0, y1 - y0)
    left = (size - (x1 - x0) * scale) / 2
    top = (size - (y1 - y0) * scale) / 2
    for y in range(size):
        for x in range(size):
            i = 4 * (y * size + x)
            if tile:
                cx, cy = (x + .5) / size, (y + .5) / size
                dx = max(abs(cx - .5) - .28, 0)
                dy = max(abs(cy - .5) - .28, 0)
                if dx * dx + dy * dy > .22 ** 2:
                    continue
                halo = max(0, 1 - math.hypot(cx - .55, cy - .38) * 1.4)
                result[i:i + 4] = bytes((round(7 + 12 * halo), round(19 + 17 * halo),
                                         round(43 + 35 * halo), 255))
            r, g, b, a = sample(src, sw, sh,
                                (x + .5 - left) / scale + x0 - .5,
                                (y + .5 - top) / scale + y0 - .5)
            if a:
                if tile:
                    bg = result[i:i + 4]
                    blend = a / 255
                    r, g, b = [round(v * blend + bg[k] * (1 - blend)) for k, v in enumerate((r, g, b))]
                    a = 255
                result[i:i + 4] = bytes((r, g, b, a))
    return bytes(result)


def make_ico(layers):
    offset = 6 + 16 * len(layers)
    entries = []
    payloads = []
    for size, image in layers:
        entries.append(struct.pack("<BBBBHHII", size if size < 256 else 0,
                                   size if size < 256 else 0, 0, 0, 1, 32,
                                   len(image), offset))
        offset += len(image)
        payloads.append(image)
    return struct.pack("<HHH", 0, 1, len(layers)) + b''.join(entries) + b''.join(payloads)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--android-output", type=Path, required=True)
    args = parser.parse_args()
    w, h, source = decode(args.source)
    box = crop_bounds(w, h, source)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "fortiq_mark.png").write_bytes(png(256, 256, render(256, source, w, h, box)))
    sizes = (16, 24, 32, 48, 64, 128, 256)
    layers = [(s, png(s, s, render(s, source, w, h, box, tile=True))) for s in sizes]
    (args.output / "fortiq.ico").write_bytes(make_ico(layers))
    # Absolute forward-slash path works with MSVC RC and MinGW windres when
    # CMake launches the compiler from a different working directory.
    ico_path = (args.output / "fortiq.ico").resolve().as_posix()
    (args.output / "fortiq.rc").write_text('IDI_ICON1 ICON "' + ico_path + '"\n', encoding="utf-8")
    for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                          ("xxhdpi", 144), ("xxxhdpi", 192)):
        folder = args.android_output / ("res/mipmap-" + density)
        folder.mkdir(parents=True, exist_ok=True)
        data = png(size, size, render(size, source, w, h, box, tile=True))
        (folder / "ic_launcher.png").write_bytes(data)
        (folder / "ic_launcher_round.png").write_bytes(data)
    drawable = args.android_output / "res/drawable-nodpi"
    drawable.mkdir(parents=True, exist_ok=True)
    (drawable / "ic_launcher_foreground.png").write_bytes(
        png(432, 432, render(432, source, w, h, box)))
    adaptive = args.android_output / "res/mipmap-anydpi-v26"
    adaptive.mkdir(parents=True, exist_ok=True)
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
           '  <background android:drawable="@color/fortiq_icon_background"/>\n'
           '  <foreground android:drawable="@drawable/ic_launcher_foreground"/>\n'
           '</adaptive-icon>\n')
    for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
        (adaptive / name).write_text(xml, encoding="utf-8")
    colors = args.android_output / "res/values"
    colors.mkdir(parents=True, exist_ok=True)
    (colors / "fortiq_colors.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        '  <color name="fortiq_icon_background">#07132B</color>\n</resources>\n',
        encoding="utf-8")
    print(f"FORTIQ branding generated: Qt PNG / Windows ICO / Android mipmaps+adaptive ({w}x{h} source)")


if __name__ == "__main__":
    main()
