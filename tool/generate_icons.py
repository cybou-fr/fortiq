"""Generates every app icon from one source image.

    python tool/generate_icons.py

Source: assets/branding/fortiq-icon-source.png (the mark on a transparent
background). Needs Pillow (`pip install pillow`).

Writes:
  assets/branding/fortiq_mark.png     cleaned mark, used inside the app
  windows/runner/resources/app_icon.ico
  android/app/src/main/res/mipmap-*/ic_launcher.png            legacy icon
  android/app/src/main/res/mipmap-*/ic_launcher_foreground.png adaptive layer
  android/app/src/main/res/mipmap-*/ic_launcher_monochrome.png themed icon (Android 13+)
  android/app/src/main/res/drawable-xxhdpi/launch_mark.png     splash screen
  ios/Runner/Assets.xcassets/AppIcon.appiconset/*.png

The icon is the mark on the app's dark background (#13141B), so it reads the
same on light and dark launchers, taskbars and home screens.
"""
import io
import json
import os
import struct

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'branding', 'fortiq-icon-source.png')
BG = (0x13, 0x14, 0x1B, 255)  # same as the app's scaffold background


def path(*parts):
    p = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def load_mark():
    """The mark alone: faint haze around it removed, edges kept smooth, cropped."""
    im = Image.open(SRC).convert('RGBA')
    alpha = im.getchannel('A').point(lambda v: 0 if v < 40 else (255 if v >= 225 else v))
    im.putalpha(alpha)
    return im.crop(alpha.getbbox())


def fit(mark, box_side):
    """Mark scaled so that its longer side is box_side px."""
    scale = box_side / max(mark.size)
    return mark.resize((max(1, round(mark.width * scale)), max(1, round(mark.height * scale))), Image.LANCZOS)


def centered(mark, canvas, frac, color=None):
    """Transparent canvas with the mark centered; its longer side = frac * canvas."""
    out = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    m = fit(mark, canvas * frac)
    if color is not None:  # single-colour silhouette
        solid = Image.new('RGBA', m.size, color)
        solid.putalpha(m.getchannel('A'))
        m = solid
    out.alpha_composite(m, ((canvas - m.width) // 2, (canvas - m.height) // 2))
    return out


def tile(mark, size, mark_frac, radius_frac):
    """Dark tile with the mark; radius_frac=0 gives a full square."""
    ss = 4  # supersample for clean rounded corners
    big = size * ss
    base = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    mask = Image.new('L', (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, big - 1, big - 1), radius=round(big * radius_frac), fill=255)
    base.paste(Image.new('RGBA', (big, big), BG), (0, 0), mask)
    base.alpha_composite(centered(mark, big, mark_frac))
    return base.resize((size, size), Image.LANCZOS)


def save_png(img, *parts):
    img.save(path(*parts), optimize=True)


def write_ico(frames, dest):
    """ICO with one PNG-compressed frame per size (Windows Vista and later)."""
    blobs = []
    for img in frames:
        buf = io.BytesIO()
        img.save(buf, format='PNG', optimize=True)
        blobs.append((img.width, img.height, buf.getvalue()))
    header = struct.pack('<HHH', 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries, data = b'', b''
    for w, h, blob in blobs:
        entries += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(blob), offset)
        data += blob
        offset += len(blob)
    with open(dest, 'wb') as f:
        f.write(header + entries + data)


def main():
    mark = load_mark()

    # ---- inside the app -------------------------------------------------
    save_png(fit(mark, 512), 'assets', 'branding', 'fortiq_mark.png')

    # ---- Windows ----------------------------------------------------------
    # Small sizes get a bigger mark and rounder-free tile so they stay readable.
    frames = []
    for s in (16, 24, 32, 48, 64, 128, 256):
        frac = 0.78 if s <= 32 else 0.62
        radius = 0.18 if s <= 32 else 0.22
        frames.append(tile(mark, s, frac, radius))
    write_ico(frames, path('windows', 'runner', 'resources', 'app_icon.ico'))

    # ---- Android ----------------------------------------------------------
    res = ('android', 'app', 'src', 'main', 'res')
    legacy = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}
    for dens, px in legacy.items():
        # Legacy launchers show the file as-is, so it carries its own rounded tile.
        save_png(tile(mark, px, 0.60, 0.22), *res, 'mipmap-' + dens, 'ic_launcher.png')
        # Adaptive layers are 108 dp; the visible/safe area is the middle 66 dp.
        layer = round(px * 108 / 48)
        save_png(centered(mark, layer, 0.43), *res, 'mipmap-' + dens, 'ic_launcher_foreground.png')
        save_png(centered(mark, layer, 0.43, color=(255, 255, 255, 255)),
                 *res, 'mipmap-' + dens, 'ic_launcher_monochrome.png')
    save_png(fit(mark, 288), *res, 'drawable-xxhdpi', 'launch_mark.png')

    # ---- iOS ----------------------------------------------------------------
    set_dir = os.path.join(ROOT, 'ios', 'Runner', 'Assets.xcassets', 'AppIcon.appiconset')
    with open(os.path.join(set_dir, 'Contents.json'), encoding='utf-8') as f:
        contents = json.load(f)
    for item in contents['images']:
        name = item.get('filename')
        if not name:
            continue
        px = round(float(item['size'].split('x')[0]) * int(item['scale'].rstrip('x')))
        # iOS icons must be opaque squares; the system rounds the corners.
        icon = tile(mark, px, 0.60, 0.0).convert('RGB')
        icon.save(os.path.join(set_dir, name), optimize=True)

    print('icons generated from', os.path.relpath(SRC, ROOT))


if __name__ == '__main__':
    main()
