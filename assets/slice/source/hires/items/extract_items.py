# /// script
# requires-python = ">=3.12"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Regenerate the 26 high-resolution item icons from the verbatim Gemini sheets.

The three sheets beside this script (health-sheet.jpg, effects-sheet.jpg,
prizes-sheet.jpg) are kept byte-for-byte as the generator returned them; their
prompts and cell orders are in the matching *.prompt.txt files. This script is
the only step between those sheets and the shipped PNGs, and it is
deterministic: running it twice writes byte-identical files.

    ./scripts/uv run --python 3.12 assets/slice/source/hires/items/extract_items.py
    ./scripts/uv run --python 3.12 assets/slice/source/hires/items/extract_items.py --review

Pipeline per item:
  1. Key distance. Each pixel gets its distance from the key along the key's own
     chroma axis, normalised by brightness: green (G - max(R, B)) / G, magenta
     (min(R, B) - G) / min(R, B). The background sits near 1, subject colours
     (including lavender and coral on magenta, mint and aqua on green) sit at or
     below 0.3, and the darker key-coloured shading the generator leaves on the
     background still reads as key. A plain RGB distance could not separate
     lavender from the magenta key, so the chroma axis is used instead.
  2. Soft alpha. Two thresholds give a linear ramp: at or below LO the pixel is
     solid, at or above HI it is background, between them it is an edge pixel.
  3. Islands. 8-connected components of the non-background mask that are
     smaller than MIN_AREA are discarded, and only components whose centroid lies
     in the item's cell are kept (so a neighbour's stray edge never leaks in).
  4. Despill. Solid pixels within SPILL_DEPTH of the edge are compared with the
     subject colour spread outwards from the deeper interior; any extra key
     chroma (the generator's rim light picks up the backdrop) is replaced by
     the interior hue at the pixel's own brightness, so the light rim survives
     but the key tint does not. Thin parts with no interior of their own (a
     floss strand) have the key chroma stripped per channel instead. Edge
     pixels take the colour of their nearest solid neighbours and an alpha
     refined by projecting onto the key-to-subject colour line.
  5. Fit. The subject is cropped to its alpha bounds, scaled with Lanczos in
     premultiplied space so its longest side fills FILL of the canvas, centred
     on an exact-size transparent canvas, and alpha below 8 is cleared.
"""
from __future__ import annotations

import argparse
import sys
from collections import deque
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
SLICE = HERE.parents[2]          # assets/slice
REPO = SLICE.parents[1]
OUT = SLICE / 'hires'
REVIEW = REPO / 'build' / 'hires-review'


@dataclass(frozen=True)
class Sheet:
    file: str
    key: str            # 'magenta' or 'green'
    cols: int           # grid the generator actually laid out
    rows: int
    size: int           # output canvas edge in pixels
    fill: float         # longest subject side as a fraction of the canvas
    lo: float           # key distance at or below which a pixel is solid
    hi: float           # key distance at or above which a pixel is background


SHEETS = {
    # Health sheet background alternates two magentas (key distance ~0.75 and
    # ~0.97), so HI sits below the lighter one.
    'health': Sheet('health-sheet.jpg', 'magenta', 6, 3, 96, 0.85, 0.40, 0.70),
    'effects': Sheet('effects-sheet.jpg', 'green', 4, 4, 96, 0.92, 0.30, 0.80),
    'prizes': Sheet('prizes-sheet.jpg', 'green', 6, 4, 64, 0.85, 0.30, 0.80),
}

# (kind, name, column, row, column span, source) in the sheet's own grid,
# zero-based. source None means the family sheet; the butterfly comes from
# the edit_image pass on the prizes sheet (see prizes-sheet-butterfly-edit.prompt.txt).
BUTTERFLY_EDIT = 'prizes-sheet-butterfly-edit.jpg'
ITEMS = (
    ('health', 'brush', 0, 0, 1, None),
    ('health', 'medicine', 1, 0, 1, None),
    ('health', 'shot', 5, 1, 1, None),
    ('health', 'wash', 3, 0, 1, None),
    ('health', 'stretch', 0, 1, 1, None),
    ('health', 'floss', 1, 1, 1, None),
    ('health', 'mouthwash', 2, 1, 1, None),
    ('health', 'spit', 3, 1, 1, None),
    ('health', 'cleanup', 4, 1, 1, None),
    ('effects', 'star', 0, 0, 1, None),
    ('effects', 'heart', 1, 0, 1, None),
    ('effects', 'orb', 2, 0, 1, None),
    ('effects', 'comet', 3, 0, 1, None),
    ('effects', 'wings', 0, 1, 1, None),
    ('effects', 'music', 1, 1, 1, None),
    ('effects', 'idea', 2, 1, 1, None),
    ('effects', 'rainbow', 3, 1, 1, None),
    ('prizes', 'butterfly', 1, 2, 1, BUTTERFLY_EDIT),
    ('prizes', 'pearl-tooth', 2, 0, 1, None),
    ('prizes', 'breakfast-sun', 3, 0, 1, None),
    ('prizes', 'tea-sprite', 4, 0, 1, None),
    ('prizes', 'movie-star', 3, 2, 1, None),
    ('prizes', 'bubble-gem', 1, 1, 1, None),
    ('prizes', 'moon-charm', 2, 1, 1, None),
    ('prizes', 'rainbow-seed', 4, 1, 1, None),
    ('prizes', 'friendship-bow', 0, 2, 1, None),
)

MARGIN = 40          # sheet pixels around a cell searched for straddling parts
MIN_AREA = 64        # components smaller than this (sheet pixels) are islands
SPILL_DEPTH = 6      # how deep into the subject the despill looks
EDGE_SEARCH = 4      # radius searched for a solid neighbour's colour
HAIRLINE_SPILL = 0.08  # key distance above which a thin part is channel-despilled
ALPHA_FLOOR = 8      # alpha below this is cleared to fully transparent
NEIGHBOURS = tuple((dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy)


def key_distance(rgb: tuple[int, int, int], key: str) -> float:
    r, g, b = rgb
    if key == 'green':
        return (g - max(r, b)) / max(g, 48)
    m = min(r, b)
    return (m - g) / max(m, 48)


def channel_despill(rgb: tuple[int, int, int], key: str) -> tuple[int, int, int]:
    """Classic despill: remove any key chroma outright (used only as a fallback)."""
    r, g, b = rgb
    if key == 'green':
        s = g - max(r, b)
        return (r, g - s, b) if s > 0 else rgb
    s = min(r, b) - g
    return (r - s, g, b - s) if s > 0 else rgb


def luma(c) -> float:
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def recolour(ref, target_luma: float) -> tuple[float, float, float]:
    """The interior colour ref, lightened or darkened to target_luma."""
    lr = luma(ref)
    if target_luma >= lr:
        t = 0.0 if lr >= 255 else (target_luma - lr) / (255 - lr)
        return tuple(v + (255 - v) * t for v in ref)
    return tuple(v * target_luma / lr for v in ref) if lr > 0 else (0.0, 0.0, 0.0)


def extract(sheet_img: Image.Image, sheet: Sheet, box: tuple[int, int, int, int]) -> Image.Image:
    """Key the subject whose components centre inside box; return a tight RGBA crop."""
    W, H = sheet_img.size
    x0, y0, x1, y1 = box
    cx0, cy0 = max(0, x0 - MARGIN), max(0, y0 - MARGIN)
    cx1, cy1 = min(W, x1 + MARGIN), min(H, y1 + MARGIN)
    crop = sheet_img.crop((cx0, cy0, cx1, cy1))
    w, h = crop.size
    n = w * h
    px = list(crop.getdata())
    lo, hi, key = sheet.lo, sheet.hi, sheet.key

    dist = [key_distance(p, key) for p in px]
    ramp = [0.0 if d >= hi else 1.0 if d <= lo else (hi - d) / (hi - lo) for d in dist]

    # Local key colour: per-channel median of clear background pixels.
    bg = [px[i] for i in range(n) if dist[i] >= hi]
    if not bg:
        raise SystemExit(f'no background found in cell {box}')
    K = tuple(sorted(c[k] for c in bg)[len(bg) // 2] for k in range(3))

    # 8-connected components of the non-background mask.
    label = [0] * n
    keep: set[int] = set()
    next_label = 0
    for start in range(n):
        if ramp[start] <= 0 or label[start]:
            continue
        next_label += 1
        label[start] = next_label
        stack = [start]
        count = sx = sy = 0
        while stack:
            i = stack.pop()
            y, x = divmod(i, w)
            count += 1; sx += x; sy += y
            for dx, dy in NEIGHBOURS:
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h:
                    j = yy * w + xx
                    if ramp[j] > 0 and not label[j]:
                        label[j] = next_label
                        stack.append(j)
        if count >= MIN_AREA:
            mx, my = sx / count + cx0, sy / count + cy0
            if x0 <= mx < x1 and y0 <= my < y1:
                keep.add(next_label)
    if not keep:
        raise SystemExit(f'no subject found in cell {box}')
    kept = [label[i] in keep for i in range(n)]
    solid = [kept[i] and ramp[i] >= 1.0 for i in range(n)]

    # Depth of each solid pixel from the subject edge (8-connected steps).
    depth = [0] * n
    queue: deque[int] = deque()
    for i in range(n):
        if not solid[i]:
            continue
        y, x = divmod(i, w)
        for dx, dy in NEIGHBOURS:
            xx, yy = x + dx, y + dy
            if not (0 <= xx < w and 0 <= yy < h) or not solid[yy * w + xx]:
                depth[i] = 1
                queue.append(i)
                break
    while queue:
        i = queue.popleft()
        y, x = divmod(i, w)
        for dx, dy in NEIGHBOURS:
            xx, yy = x + dx, y + dy
            if 0 <= xx < w and 0 <= yy < h:
                j = yy * w + xx
                if solid[j] and not depth[j]:
                    depth[j] = depth[i] + 1
                    queue.append(j)

    # Interior reference colour for every solid pixel near the edge. It spreads
    # outwards from seed pixels layer by layer, each pixel averaging the
    # references of neighbours reached in earlier layers.
    def neighbours(i: int):
        y, x = divmod(i, w)
        for dx, dy in NEIGHBOURS:
            xx, yy = x + dx, y + dy
            if 0 <= xx < w and 0 <= yy < h:
                yield yy * w + xx

    colour: list = [px[i] if solid[i] else None for i in range(n)]
    ref: list = [None] * n

    def spread(layer: list[int], steps: int | None) -> None:
        while layer and steps != 0:
            frontier = sorted({j for i in layer for j in neighbours(i) if solid[j] and ref[j] is None})
            refs = []
            for j in frontier:
                near = [ref[k] for k in neighbours(j) if ref[k] is not None]
                refs.append(tuple(sum(c[k] for c in near) / len(near) for k in range(3)))
            for j, r in zip(frontier, refs):
                ref[j] = r
            layer = frontier
            steps = None if steps is None else steps - 1

    # Seeds are the true interior; the reference spreads only as far as the
    # despill band, so a thin part of another colour (a floss strand) is never
    # repainted with the colour of the body it hangs from.
    deep = [i for i in range(n) if solid[i] and depth[i] > SPILL_DEPTH]
    for i in deep:
        ref[i] = px[i]
    spread(deep, SPILL_DEPTH + 3)
    for i in range(n):
        if not solid[i] or depth[i] > SPILL_DEPTH:
            continue
        C = px[i]
        if ref[i] is None:
            # A thin part with no interior of its own: strip key chroma outright.
            if key_distance(C, key) > HAIRLINE_SPILL:
                colour[i] = channel_despill(C, key)
            continue
        excess = key_distance(C, key) - key_distance(tuple(round(v) for v in ref[i]), key)
        if excess > 0.04:
            t = min(1.0, (excess - 0.04) / 0.2)
            target = recolour(ref[i], luma(C))
            colour[i] = tuple(C[k] + (target[k] - C[k]) * t for k in range(3))

    # Edge pixels: colour of nearby solid pixels, alpha by projection.
    out = [(0, 0, 0, 0)] * n
    for i in range(n):
        if not kept[i]:
            continue
        if solid[i]:
            c = colour[i]
            out[i] = (round(c[0]), round(c[1]), round(c[2]), 255)
            continue
        y, x = divmod(i, w)
        F = None
        for rad in range(1, EDGE_SEARCH + 1):
            acc = [0.0, 0.0, 0.0]
            m = 0
            for yy in range(max(0, y - rad), min(h, y + rad + 1)):
                for xx in range(max(0, x - rad), min(w, x + rad + 1)):
                    j = yy * w + xx
                    if solid[j]:
                        c = colour[j]
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; m += 1
            if m:
                F = (acc[0] / m, acc[1] / m, acc[2] / m)
                break
        C = px[i]
        if F is not None:
            d = (F[0] - K[0], F[1] - K[1], F[2] - K[2])
            dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2]
            if dd >= 1600:
                t = ((C[0] - K[0]) * d[0] + (C[1] - K[1]) * d[1] + (C[2] - K[2]) * d[2]) / dd
                t = min(1.0, max(0.0, t))
                out[i] = (round(F[0]), round(F[1]), round(F[2]), round(t * 255))
                continue
        r, g, b = channel_despill(C, key)
        out[i] = (r, g, b, round(ramp[i] * 255))

    im = Image.new('RGBA', (w, h))
    im.putdata(out)
    bounds = im.getchannel('A').point(lambda a: 255 if a >= ALPHA_FLOOR else 0).getbbox()
    return im.crop(bounds)


def fit(subject: Image.Image, size: int, fill: float) -> Image.Image:
    scale = fill * size / max(subject.size)
    nw = max(1, round(subject.width * scale))
    nh = max(1, round(subject.height * scale))
    small = subject.convert('RGBa').resize((nw, nh), Image.Resampling.LANCZOS).convert('RGBA')
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    canvas.paste(small, ((size - nw) // 2, (size - nh) // 2))
    data = [(0, 0, 0, 0) if a < ALPHA_FLOOR else (r, g, b, a) for r, g, b, a in canvas.getdata()]
    canvas.putdata(data)
    return canvas


def cell_box(sheet: Sheet, size: tuple[int, int], col: int, row: int, span: int):
    W, H = size
    return (round(col * W / sheet.cols), round(row * H / sheet.rows),
            round((col + span) * W / sheet.cols), round((row + 1) * H / sheet.rows))


def build() -> list[Path]:
    written = []
    images: dict[str, Image.Image] = {}
    for kind, name, col, row, span, source in ITEMS:
        sheet = SHEETS[kind]
        source = source or sheet.file
        if source not in images:
            images[source] = Image.open(HERE / source).convert('RGB')
        img = images[source]
        subject = extract(img, sheet, cell_box(sheet, img.size, col, row, span))
        icon = fit(subject, sheet.size, sheet.fill)
        path = OUT / kind / f'{name}.png'
        path.parent.mkdir(parents=True, exist_ok=True)
        icon.save(path, format='PNG', optimize=False, compress_level=9)
        written.append(path)
    return written


# ---------------------------------------------------------------- review sheets

def _font(size: int):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:  # pragma: no cover - Pillow without FreeType
        return ImageFont.load_default()


def _disk(size: int, layers, ss: int = 4) -> Image.Image:
    """Anti-aliased concentric disks: layers = [(radius, colour), ...] outermost first."""
    big = Image.new('RGBA', (size * ss, size * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * ss / 2
    for radius, colour in layers:
        r = radius * ss
        d.ellipse((c - r, c - r, c + r, c + r), fill=colour)
    return big.resize((size, size), Image.Resampling.LANCZOS)


def _shrink(icon: Image.Image, size: int) -> Image.Image:
    if icon.width == size:
        return icon
    return icon.convert('RGBa').resize((size, size), Image.Resampling.LANCZOS).convert('RGBA')


def review() -> list[Path]:
    REVIEW.mkdir(parents=True, exist_ok=True)
    font = _font(13)
    names = {kind: [n for k, n, *_ in ITEMS if k == kind] for kind in SHEETS}
    icons = {(k, n): Image.open(OUT / k / f'{n}.png') for k, n, *_ in ITEMS}
    paths = []

    # Health: 96 px on a dark-grey disk (as in the ring buttons).
    cell, cols = 140, 5
    items = names['health']
    rows = -(-len(items) // cols)
    sheet = Image.new('RGB', (cell * cols, cell * rows + 10), (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    disk = _disk(120, [(60, (0x4a, 0x4a, 0x52, 255))])
    for i, name in enumerate(items):
        x, y = (i % cols) * cell, (i // cols) * cell
        sheet.paste(disk, (x + 10, y + 4), disk)
        sheet.paste(icons['health', name], (x + 22, y + 16), icons['health', name])
        d.text((x + cell // 2, y + cell - 12), name, fill=(220, 220, 225), font=font, anchor='mm')
    paths.append(REVIEW / 'items-health.png'); sheet.save(paths[-1])

    # Effects: 16, 32 and 48 px on near-black, plus 4x nearest views of 16 and 32.
    items = names['effects']
    col_w = [40, 56, 72, 84, 148]
    heads = ['16', '32', '48', '16 x4', '32 x4']
    row_h = 140
    W = 120 + sum(col_w) + 40
    sheet = Image.new('RGB', (W, 30 + row_h * len(items)), (16, 16, 16))
    d = ImageDraw.Draw(sheet)
    x = 120
    for head, cw in zip(heads, col_w):
        d.text((x + cw // 2, 14), head, fill=(160, 160, 165), font=font, anchor='mm'); x += cw
    for r, name in enumerate(items):
        y = 30 + r * row_h
        d.text((10, y + row_h // 2), name, fill=(220, 220, 225), font=font, anchor='lm')
        x = 120
        for size, cw in zip((16, 32, 48), col_w):
            im = _shrink(icons['effects', name], size)
            sheet.paste(im, (x + (cw - size) // 2, y + (row_h - size) // 2), im); x += cw
        for size, cw in zip((16, 32), col_w[3:]):
            im = _shrink(icons['effects', name], size)
            im = im.resize((size * 4, size * 4), Image.Resampling.NEAREST)
            if im.width > cw:
                im = im.resize((cw - 4, cw - 4), Image.Resampling.NEAREST)
            sheet.paste(im, (x + (cw - im.width) // 2, y + (row_h - im.height) // 2), im); x += cw
    paths.append(REVIEW / 'items-effects.png'); sheet.save(paths[-1])

    # Prizes: 64 px inside the gold-rimmed disk, and a 3x3 collection grid.
    items = names['prizes']
    disk = _disk(80, [(39, (0xf5, 0xc7, 0x64, 255)), (35, (0x2a, 0x2a, 0x30, 255))])
    cell = 110
    left = Image.new('RGB', (cell * 3, cell * 3), (24, 24, 28))
    d = ImageDraw.Draw(left)
    for i, name in enumerate(items):
        x, y = (i % 3) * cell, (i // 3) * cell
        left.paste(disk, (x + 15, y + 4), disk)
        left.paste(icons['prizes', name], (x + 23, y + 12), icons['prizes', name])
        d.text((x + cell // 2, y + cell - 12), name, fill=(220, 220, 225), font=font, anchor='mm')
    grid = Image.new('RGB', (3 * 72 + 8, 3 * 72 + 8), (0x29, 0x1b, 0x35))
    for i, name in enumerate(items):
        x, y = 4 + (i % 3) * 72, 4 + (i // 3) * 72
        ImageDraw.Draw(grid).rounded_rectangle((x + 1, y + 1, x + 70, y + 70), 10, fill=(0x49, 0x33, 0x4f))
        grid.paste(icons['prizes', name], (x + 4, y + 4), icons['prizes', name])
    sheet = Image.new('RGB', (left.width + grid.width + 30, max(left.height, grid.height) + 20), (24, 24, 28))
    sheet.paste(left, (0, 10))
    sheet.paste(grid, (left.width + 20, 10))
    paths.append(REVIEW / 'items-prizes.png'); sheet.save(paths[-1])
    return paths


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--review', action='store_true', help='also write build/hires-review/items-*.png')
    args = parser.parse_args(argv)
    for path in build():
        print(path.relative_to(REPO))
    if args.review:
        for path in review():
            print(path.relative_to(REPO))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
