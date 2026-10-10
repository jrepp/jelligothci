# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Reproduce the four additional 64px location scenes in the existing gray palette.

Only writes park/pond/beach/library PNGs and their manifest records. Existing
Home/Garden and hand-painted creature/icon artwork are preserved.
"""
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('park', 'pond', 'beach', 'library')
PALETTE = [f'#{v:02x}{v:02x}{v:02x}' for v in range(0, 211, 14)]


def park(d):
    d.rectangle((0, 36, 63, 63), fill=126)
    d.polygon([(27, 34), (35, 34), (48, 63), (15, 63)], fill=168)
    for x, y in [(11, 24), (53, 27)]:
        d.rectangle((x-1, y, x+1, y+16), fill=70)
        d.ellipse((x-7, y-11, x+7, y+4), fill=98)
        d.rectangle((x-5, y-6, x+4, y-3), fill=112)
    d.rectangle((43, 40, 55, 42), fill=70)
    d.rectangle((44, 36, 54, 38), fill=98)
    d.line((45, 39, 45, 46), fill=70)
    d.line((53, 39, 53, 46), fill=70)
    for x, y in [(11, 46), (17, 42), (52, 51), (12, 53)]:
        d.line((x, y, x+2, y), fill=98)


def pond(d):
    d.polygon([(0, 29), (15, 26), (29, 30), (46, 24), (63, 29), (63, 63), (0, 63)], fill=112)
    d.ellipse((6, 26, 60, 62), fill=154)
    d.ellipse((9, 28, 58, 59), fill=168)
    for x, y in [(45, 31), (12, 32), (45, 40), (32, 28)]:
        d.line((x, y, x+6, y), fill=140)
    d.rectangle((5, 36, 21, 41), fill=84)
    for x in range(7, 22, 4):
        d.line((x, 36, x, 41), fill=112)
    for x, y in [(12, 27), (52, 27), (56, 33)]:
        d.line((x, y+7, x, y-3), fill=70)
        d.rectangle((x-1, y-5, x+1, y-1), fill=98)
    d.ellipse((46, 35, 52, 38), fill=112)
    d.point((49, 35), fill=182)


def beach(d):
    d.rectangle((0, 29, 63, 43), fill=126)
    for y in (31, 36, 40):
        d.line((6, y, 58, y), fill=154)
    d.polygon([(0, 42), (17, 39), (35, 44), (63, 39), (63, 63), (0, 63)], fill=182)
    d.line([(0, 42), (17, 39), (35, 44), (63, 39)], fill=210, width=2)
    d.line((49, 31, 49, 50), fill=98)
    d.polygon([(40, 33), (43, 27), (49, 25), (55, 27), (58, 33)], fill=112)
    d.polygon([(49, 25), (46, 33), (52, 33)], fill=154)
    d.rectangle((42, 50, 54, 53), fill=140)
    for x, y in [(13, 48), (20, 52), (34, 49)]:
        d.line((x, y, x+1, y), fill=140)


def library(d):
    d.rectangle((0, 40, 63, 63), fill=126)
    for x0 in (7, 46):
        d.rectangle((x0, 17, x0+10, 43), fill=70)
        for y in (18, 26, 34):
            d.rectangle((x0+1, y, x0+9, y+6), fill=98)
            for dx, h, tone in [(2, 5, 154), (5, 4, 126), (8, 6, 182)]:
                d.rectangle((x0+dx, y+6-h, x0+dx+1, y+5), fill=tone)
    d.rectangle((22, 13, 41, 20), fill=154)
    d.rectangle((23, 14, 40, 19), fill=182)
    d.line((32, 14, 32, 19), fill=126)
    d.rectangle((17, 46, 46, 49), fill=84)
    d.rectangle((20, 50, 22, 56), fill=98)
    d.rectangle((42, 50, 44, 56), fill=98)
    d.polygon([(27, 43), (32, 44), (37, 43), (39, 46), (32, 47), (25, 46)], fill=182)
    d.line((32, 44, 32, 46), fill=126)


def scene(name):
    image = Image.new('L', (64, 64), 168)
    globals()[name](ImageDraw.Draw(image))
    out = Image.new('RGBA', image.size)
    for y in range(64):
        for x in range(64):
            radius = math.hypot(x-31.5, y-31.5)/32
            vignette = max(0, 1-max(0, radius-.60)/.40)**.8
            value = max(0, min(210, round(image.getpixel((x, y))*vignette/14)*14))
            out.putpixel((x, y), (value, value, value, 255))
    return out


def create_location_assets(root):
    records = []
    for index, name in enumerate(NAMES):
        image = scene(name)
        image.save(root / f'backgrounds/{name}.png')
        records.append(dict(id=10003+index, key=f'backgrounds.{name}',
                            path=f'backgrounds/{name}.png', kind='backgrounds',
                            width=64, height=64, pivot=[32, 32], bounds=[0, 0, 64, 64],
                            palette=PALETTE, purpose=f'Quiet {name} scene with a black vignette'))
    return records


if __name__ == '__main__':
    path = ROOT / 'assets.json'
    manifest = json.loads(path.read_text())
    records = create_location_assets(ROOT)
    keys = {record['key'] for record in records}
    manifest['assets'] = [a for a in manifest['assets'] if a['key'] not in keys] + records
    path.write_text(json.dumps(manifest, indent=2)+'\n')
