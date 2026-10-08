"""Ninja pet frames cut from the generated ninja atlas; shared palette, binary alpha.

The source sheet (`ninja-atlas.jpg`, prompt in `ninja-imagegen-prompt.txt`) is a
1024x1024 JPEG: a 4x4 grid of 256px cells on flat magenta, poses in POSES order
in the first eight cells. The image generator drew on a logical pixel grid of
about 8 source pixels, so each frame is recovered by majority vote per logical
pixel rather than by resampling, then placed on the shared creature ground line.

Reproducibility: Pillow is pinned to 12.0.0 by the caller's inline script
metadata, and that wheel bundles its own libjpeg-turbo, so decoding the JPEG is
deterministic and `authoring.py` regenerates these PNGs byte-identically.
"""
from collections import Counter
from functools import lru_cache

from PIL import Image

POSES = ('idle-a', 'idle-b', 'eating', 'happy', 'asleep', 'unwell', 'curious', 'content')
# Stable runtime IDs, in pose order; the C frame table (core/pet_actor.c) mirrors these.
FIRST_ID = 1031
PITCH = 8.0          # Source pixels per logical sprite pixel, fixed for the whole sheet.
CELL_MARGIN = 12     # Ignore cell edges when locating each sprite.
PHASE_SEARCH = 4     # Per-cell grid-phase search, in source pixels each way.
OPEN_EYES = frozenset(('idle-a', 'idle-b', 'eating', 'curious'))


def is_background(r, g, b):
    """Flat magenta plus its JPEG fringe."""
    return r > 190 and b > 190 and g < 170


def create_ninja_assets(root, palette, atlas_path):
    """Write creatures/ninja-<pose>.png under root; return manifest records in pose order."""
    rgb = [tuple(bytes.fromhex(c[1:])) for c in palette]

    @lru_cache(maxsize=None)
    def nearest(color):
        return min(range(len(rgb)), key=lambda i: sum((rgb[i][j] - color[j]) ** 2 for j in range(3)))

    atlas = Image.open(atlas_path).convert('RGB')
    if atlas.size != (1024, 1024):
        raise ValueError(f'Unexpected ninja atlas size: {atlas.size}')
    px = atlas.load()

    def votes(box, row, col):
        """Classify the interior of one logical pixel; -1 is background or outside."""
        left, top = box[0], box[1]
        tally = Counter()
        for y in range(int(top + row * PITCH + 1), int(top + (row + 1) * PITCH - 1)):
            for x in range(int(left + col * PITCH + 1), int(left + (col + 1) * PITCH - 1)):
                if not (0 <= x < atlas.width and 0 <= y < atlas.height):
                    tally[-1] += 1
                    continue
                color = px[x, y]
                tally[-1 if is_background(*color) else nearest(color)] += 1
        return tally

    def grid(box):
        return round((box[3] - box[1]) / PITCH), round((box[2] - box[0]) / PITCH)

    def block_score(box):
        """Mean fraction of source pixels disagreeing with their block's majority class."""
        rows, cols = grid(box)
        bad = total = 0
        for row in range(rows):
            for col in range(cols):
                tally = votes(box, row, col)
                if tally:
                    count = sum(tally.values())
                    bad += count - tally.most_common(1)[0][1]
                    total += count
        return bad / max(total, 1)

    def downsample(box):
        rows, cols = grid(box)
        out = Image.new('RGBA', (cols, rows))
        for row in range(rows):
            for col in range(cols):
                index = votes(box, row, col).most_common(1)[0][0]
                out.putpixel((col, row), (0, 0, 0, 0) if index < 0 else (*rgb[index], 255))
        return out

    def cell_bbox(x0, y0, size):
        xs, ys = [], []
        for y in range(y0 + CELL_MARGIN, y0 + size - CELL_MARGIN):
            for x in range(x0 + CELL_MARGIN, x0 + size - CELL_MARGIN):
                if not is_background(*px[x, y]):
                    xs.append(x)
                    ys.append(y)
        return min(xs), min(ys), max(xs) + 1, max(ys) + 1

    (root / 'creatures').mkdir(parents=True, exist_ok=True)
    cell = atlas.width // 4
    records = []
    for index, pose in enumerate(POSES):
        left, top, right, bottom = cell_bbox((index % 4) * cell, (index // 4) * cell, cell)
        # Align the sampling grid with the art's own pixel grid in this cell.
        candidates = []
        for dy in range(-PHASE_SEARCH, PHASE_SEARCH + 1):
            for dx in range(-PHASE_SEARCH, PHASE_SEARCH + 1):
                box = (left + dx, top + dy, right + dx + PITCH, bottom + dy + PITCH)
                candidates.append((block_score(box), box))
        small = downsample(min(candidates)[1])
        small = small.crop(small.getchannel('A').getbbox())
        if small.width > 30 or small.height > 30:
            raise ValueError(f'ninja-{pose}: {small.size} exceeds the 32px frame')
        frame = Image.new('RGBA', (32, 32))
        # Centre at x=16 with the feet on y=30, matching the creature pivot [16, 30].
        frame.paste(small, (16 - small.width // 2, 30 - small.height))
        paintover(frame, pose, rgb)
        path = f'creatures/ninja-{pose}.png'
        frame.save(root / path)
        records.append(dict(id=FIRST_ID + index, key=f'creatures.ninja-{pose}', path=path,
                            kind='creatures', width=32, height=32, pivot=[16, 30],
                            bounds=list(frame.getchannel('A').getbbox()), form='ninja', pose=pose))
    return records


def paintover(frame, pose, rgb):
    """Deterministic cleanup of JPEG spill: gold only on the rice ball, crisp 2x2 eyes.

    Gold is allowed only in the eating pose; elsewhere it becomes cream. In
    open-eyed poses, each small dark blob fully enclosed by face colours
    (cream/pink) becomes a 2x2 deep-plum eye at its densest position.
    """
    deep, dark, cream, pink, gold = rgb[0], rgb[1], rgb[10], rgb[13], rgb[14]
    px = frame.load()
    w, h = frame.size
    if pose != 'eating':
        for y in range(h):
            for x in range(w):
                if px[x, y][3] and px[x, y][:3] == gold:
                    px[x, y] = (*cream, 255)
    if pose not in OPEN_EYES:
        return
    dark_cells = {(x, y) for y in range(h) for x in range(w)
                  if px[x, y][3] and px[x, y][:3] in (deep, dark)}
    seen = set()
    for start in sorted(dark_cells):
        if start in seen:
            continue
        component, stack = [], [start]
        seen.add(start)
        while stack:
            x, y = stack.pop()
            component.append((x, y))
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if n in dark_cells and n not in seen:
                    seen.add(n)
                    stack.append(n)
        xs = [x for x, _ in component]
        ys = [y for _, y in component]
        if len(component) > 8 or max(xs) - min(xs) > 2 or max(ys) - min(ys) > 2:
            continue  # Body, mask edge, or outline, not an eye.
        cells = set(component)
        ring = {(x + dx, y + dy) for x, y in component
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))} - cells
        if any(not (0 <= x < w and 0 <= y < h) or px[x, y][3] == 0 or px[x, y][:3] not in (cream, pink)
               for x, y in ring):
            continue  # An eye is fully enclosed by the face; headband and outline are not.
        best = max(((x, y) for y in range(min(ys), max(ys) + 1) for x in range(min(xs), max(xs) + 1)),
                   key=lambda c: (sum((c[0] + i, c[1] + j) in cells for i in (0, 1) for j in (0, 1)),
                                  -c[1], -c[0]))
        for x, y in component:
            px[x, y] = (*cream, 255)
        for i in (0, 1):
            for j in (0, 1):
                px[best[0] + i, best[1] + j] = (*deep, 255)
