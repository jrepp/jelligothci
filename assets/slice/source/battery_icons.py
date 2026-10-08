"""Original 16px battery-status icons; shared palette, binary alpha, code-authored pixels.

Level icons differ by bar count (0-3) and the empty icon adds an exclamation
mark, so charge level never depends on color alone. IDs 12001-12007 match
JelliBatteryIcon in include/jelli/power.h.
"""
from PIL import Image, ImageDraw

NAMES = ('empty', 'low', 'medium', 'high', 'charging', 'plugged', 'unknown')
PURPOSES = (
    'Battery below 10 percent: empty cell with an exclamation mark',
    'Battery 10-24 percent: one bar; caption also reads LOW',
    'Battery 25-69 percent: two bars',
    'Battery 70-100 percent or charge complete: three bars',
    'Battery charging: cell with a lightning bolt',
    'External power without a battery: plug',
    'Battery state or level unknown: cell with a question mark',
)


def shell(d, ink, cream, cavity):
    """Rounded cell: ink outline, cream case, dark 11x6 cavity at x2..12, y5..10."""
    d.rectangle((1, 3, 13, 3), fill=ink)
    d.rectangle((1, 12, 13, 12), fill=ink)
    d.rectangle((0, 4, 0, 11), fill=ink)
    d.rectangle((14, 4, 14, 11), fill=ink)
    d.rectangle((1, 4, 13, 11), fill=cream)
    d.rectangle((2, 5, 12, 10), fill=cavity)
    d.rectangle((14, 6, 14, 9), fill=cream)  # Terminal: case bulges through the wall.
    d.rectangle((15, 6, 15, 9), fill=ink)


def bars(d, count, fill, shine):
    """Three 3px bars with 1px gaps fill the cavity exactly; a lighter top row adds depth."""
    for index in range(count):
        left = 2 + index * 4
        d.rectangle((left, 5, left + 2, 10), fill=fill)
        d.rectangle((left, 5, left + 2, 5), fill=shine)


def outline(image, ink):
    """Ink edge on transparent pixels touching artwork, as in the other 16px icons."""
    width, height = image.size
    pixels = image.load()
    edge = []
    for y in range(height):
        for x in range(width):
            if pixels[x, y][3]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < width and 0 <= ny < height and pixels[nx, ny][3]:
                    edge.append((x, y))
                    break
    for x, y in edge:
        pixels[x, y] = (*ink, 255)


def bolt(image, ink, gold, cream):
    """Lightning bolt over the cell, outlined so it separates from the case."""
    d = ImageDraw.Draw(image)
    mask = Image.new('RGBA', image.size)
    m = ImageDraw.Draw(mask)
    m.polygon([(9, 1), (4, 8), (7, 8), (6, 14), (11, 7), (8, 7), (10, 1)], fill=(*gold, 255))
    edge = Image.new('RGBA', image.size)
    edge.paste(mask, (0, 0), mask)
    outline(edge, ink)
    image.paste(edge, (0, 0), edge)
    d.point((8, 3), fill=cream)


def plug(d, gold, mint, teal, cream):
    """Two prongs, a head that tapers into the cord, and a small highlight."""
    d.rectangle((5, 1, 6, 4), fill=gold)
    d.rectangle((9, 1, 10, 4), fill=gold)
    d.rectangle((4, 5, 11, 8), fill=mint)
    d.rectangle((5, 9, 10, 9), fill=mint)
    d.rectangle((6, 10, 9, 10), fill=mint)
    d.rectangle((5, 6, 6, 6), fill=cream)
    d.rectangle((7, 11, 8, 14), fill=teal)


def create_battery_assets(root, palette):
    c = [tuple(bytes.fromhex(v[1:])) for v in palette]
    ink, shadow, teal, mint, light, cream, coral, pink, gold, pale = [
        c[i] for i in (0, 1, 3, 4, 5, 10, 12, 13, 14, 9)]
    (root / 'battery').mkdir(parents=True, exist_ok=True)
    records = []
    for index, name in enumerate(NAMES):
        im = Image.new('RGBA', (16, 16))
        d = ImageDraw.Draw(im)
        if name == 'plugged':
            plug(d, gold, light, teal, cream)
            outline(im, ink)
        else:
            shell(d, ink, cream, shadow)
        if name == 'empty':
            d.rectangle((7, 5, 7, 8), fill=coral)
            d.point((7, 10), fill=coral)
        elif name == 'low':
            bars(d, 1, coral, pink)
        elif name == 'medium':
            bars(d, 2, gold, cream)
        elif name == 'high':
            bars(d, 3, mint, light)
        elif name == 'charging':
            bolt(im, ink, gold, cream)
        elif name == 'unknown':
            for x, y in ((6, 5), (7, 5), (8, 5), (5, 6), (9, 6), (8, 7), (7, 8), (7, 10)):
                d.point((x, y), fill=pale)
        path = f'battery/{name}.png'
        im.save(root / path)
        records.append(dict(id=12001 + index, key=f'battery.{name}', path=path, kind='battery',
                            width=16, height=16, pivot=[8, 8],
                            bounds=list(im.getchannel('A').getbbox()), purpose=PURPOSES[index]))
    return records
