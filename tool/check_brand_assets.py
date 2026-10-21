#!/usr/bin/env python3
"""Static format/packaging checks for generated FORTIQ native icons."""
import argparse
import struct
import sys
from pathlib import Path
from xml.etree import ElementTree

def check_png(path, size):
    raw = path.read_bytes()
    assert raw.startswith(b'\x89PNG\r\n\x1a\n'), f"not PNG: {path}"
    assert raw[12:16] == b'IHDR', f"no IHDR: {path}"
    w, h = struct.unpack_from('>II', raw, 16)
    assert (w, h) == (size, size), f"bad PNG dimensions: {path}: {w}x{h}"

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--android-output', type=Path, required=True)
    args = p.parse_args()
    root, a = args.output, args.android_output
    check_png(root / 'fortiq_mark.png', 256)
    ico = (root / 'fortiq.ico').read_bytes()
    reserved, typ, count = struct.unpack_from('<HHH', ico)
    assert (reserved, typ, count) == (0, 1, 7)
    dimensions = []
    for n in range(count):
        width, height, colors, res, planes, bpp, length, offset = struct.unpack_from(
            '<BBBBHHII', ico, 6 + 16*n)
        size = 256 if width == 0 else width
        assert size == (256 if height == 0 else height)
        assert (planes, bpp) == (1, 32)
        assert offset + length <= len(ico)
        data = ico[offset:offset+length]
        assert data.startswith(b'\x89PNG\r\n\x1a\n')
        dimensions.append(size)
    assert dimensions == [16, 24, 32, 48, 64, 128, 256], dimensions
    rc = (root / 'fortiq.rc').read_text('utf-8')
    assert 'IDI_ICON1 ICON' in rc and 'fortiq.ico' in rc
    for density, size in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96),
                          ('xxhdpi', 144), ('xxxhdpi', 192)):
        for name in ('ic_launcher.png', 'ic_launcher_round.png'):
            check_png(a / ('res/mipmap-' + density) / name, size)
    for name in ('ic_launcher_foreground.png', 'ic_launcher_monochrome.png'):
        check_png(a / 'res/drawable-nodpi' / name, 432)
    xml = a / 'res/mipmap-anydpi-v26/ic_launcher.xml'
    rootxml = ElementTree.parse(xml).getroot()
    assert rootxml.tag == 'adaptive-icon'
    names = [node.tag for node in rootxml]
    assert names == ['background','foreground','monochrome'], names
    assert (a/'res/values/fortiq_colors.xml').is_file()
    print('PASS: Windows ICO/RC, Qt mark PNG, Android density/adaptive/monochrome assets')

if __name__ == '__main__':
    main()
