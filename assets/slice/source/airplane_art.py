# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Reproducible airplane pixel kit; blue aisle visitors reuse existing Jelli art."""
import argparse
import json
import sys
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
PALETTE = ['#293644', '#42566a', '#63798b', '#8da0a8', '#c2cbc4', '#ebe5d3',
           '#fff4cf', '#8ac3d1', '#b9dde1', '#e4f2e9', '#7c6962', '#ad9382']
BLUE = {'#187b79': '#346b9b', '#43bca2': '#65a7d5', '#85e4b6': '#a4d9ed',
        '#cff5cf': '#e2f5fa'}


def remap_image(image, mapping):
    """Apply simultaneous RGB replacements while preserving source alpha."""
    lookup = {tuple(bytes.fromhex(source[1:])): tuple(bytes.fromhex(target[1:]))
              for source, target in mapping.items()}
    result = image.convert('RGBA')
    result.putdata([(*lookup.get(pixel[:3], pixel[:3]), pixel[3]) if pixel[3] else pixel
                    for pixel in result.getdata()])
    return result


def artwork():
    c = PALETTE
    sky = Image.new('RGBA', (64, 64), c[7])
    d = ImageDraw.Draw(sky)
    d.rectangle((0, 22, 63, 63), fill=c[8])
    clouds = Image.new('RGBA', (64, 64))
    d = ImageDraw.Draw(clouds)
    for x, y in ((0, 18), (30, 12), (49, 25)):
        d.rectangle((x, y, x+17, y+2), fill=c[9])
        d.rectangle((x+4, y-3, x+12, y), fill=c[9])
        d.line((x+2, y+3, x+15, y+3), fill=c[8])
    cabin = Image.new('RGBA', (64, 64), c[4])
    d = ImageDraw.Draw(cabin)
    d.rectangle((0, 0, 63, 6), fill=c[3])
    d.line((0, 7, 63, 7), fill=c[5])
    d.line((0, 9, 63, 9), fill=c[3])
    d.line((10, 10, 10, 38), fill=c[3])
    d.line((62, 10, 62, 38), fill=c[3])
    # Rounded porthole with a thick recessed rim; the hole reveals moving sky.
    d.rounded_rectangle((43, 11, 59, 32), radius=6, fill=c[3])
    d.rounded_rectangle((44, 12, 58, 31), radius=5, fill=c[5])
    d.rounded_rectangle((46, 14, 56, 29), radius=4, fill=c[1])
    d.rounded_rectangle((47, 15, 55, 28), radius=3, fill=(0, 0, 0, 0))
    d.line((49, 12, 54, 12), fill=c[6])
    d.rectangle((0, 38, 63, 63), fill=c[1])
    d.rectangle((0, 40, 63, 41), fill=c[0])
    d.rectangle((0, 46, 63, 57), fill=c[2])
    d.line((0, 45, 63, 45), fill=c[3])
    d.line((0, 58, 63, 58), fill=c[0])
    for x in range(4, 64, 8):
        d.point((x, 61), fill=c[3])
    # Central seat supports the actor's existing source-space foot anchor (~35px).
    d.rounded_rectangle((21, 20, 41, 34), radius=3, fill=c[0])
    d.rounded_rectangle((23, 21, 39, 33), radius=2, fill=c[2])
    d.rectangle((24, 22, 38, 24), fill=c[3])
    d.rectangle((23, 32, 39, 36), fill=c[2])
    d.line((24, 33, 38, 33), fill=c[3])
    d.rectangle((20, 31, 22, 35), fill=c[0])
    d.rectangle((40, 31, 42, 35), fill=c[0])
    d.line((20, 31, 22, 31), fill=c[5])
    d.line((40, 31, 42, 31), fill=c[5])
    d.rectangle((24, 37, 26, 41), fill=c[0])
    d.rectangle((36, 37, 38, 41), fill=c[0])
    source = Image.open(ROOT / 'assets/slice/creatures/baby-idle-a.png').convert('RGBA')
    visitor = Image.new('RGBA', (64, 64))
    blue = remap_image(source, BLUE)
    crop = blue.crop(blue.getbbox())
    crop.thumbnail((13, 13), Image.Resampling.NEAREST)
    visitor.alpha_composite(crop, (0, 0))
    icon = Image.new('RGBA', (16, 16))
    d = ImageDraw.Draw(icon)
    d.polygon([(7, 1), (9, 1), (9, 6), (14, 10), (14, 12), (9, 10),
               (9, 13), (11, 14), (11, 15), (5, 15), (5, 14), (7, 13),
               (7, 10), (2, 12), (2, 10), (7, 6)], fill=c[5])
    d.line((8, 3, 8, 12), fill=c[3])
    return {'airplane-sky': sky, 'airplane-clouds': clouds, 'airplane-cabin': cabin,
            'airplane-visitor': visitor, 'airplane': icon}


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'assets/slice/assets.json').read_text())
    records = []
    for i, (name, im) in enumerate(artwork().items()):
        kind = 'icons' if name == 'airplane' else 'backgrounds'
        path = f'{kind}/{name}.png'
        (output / kind).mkdir(exist_ok=True)
        im.save(output / path)
        palette = [BLUE.get(v, v) for v in manifest['palette']] if name.endswith('visitor') else PALETTE
        records.append({'id': 2013 if kind == 'icons' else 10016+i,
                        'key': f'{kind}.{name}', 'path': path, 'kind': kind,
                        'width': im.width, 'height': im.height,
                        'pivot': [im.width//2, im.height//2],
                        'bounds': list(im.getbbox()), 'palette': palette,
                        'purpose': 'Airplane travel kit; reproducible in source/airplane_art.py'})
    (output / 'airplane.json').write_text(json.dumps(records, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/assets/airplane')
    build(parser.parse_args().output)
