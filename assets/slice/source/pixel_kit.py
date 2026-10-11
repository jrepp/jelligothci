"""Small pixel-art toolkit shared by the polish recipes.

A sprite is a dict mapping (x, y) to a palette name. Shapes are authored as
row spans so curves stay deliberate, then outlined and shaded by the same rules
for every asset (see docs/pixel-art-guide.md).
"""
import json
from pathlib import Path

from PIL import Image

# Role names for the 16 shared palette slots, in manifest order. The colours
# themselves come from assets.json so a palette edit flows through every recipe.
NAMES = ("ink", "plum", "mauve", "teal", "sea", "mint", "foam", "violet",
         "lilac", "haze", "cream", "white", "rose", "coral", "gold", "tan")
_MANIFEST = Path(__file__).resolve().parents[1] / "assets.json"
PALETTE = dict(zip(NAMES, json.loads(_MANIFEST.read_text())["palette"]))
# light, base, shadow, deep for each material.
RAMPS = {
    "mint": ("foam", "mint", "sea", "teal"),
    "haze": ("white", "haze", "lilac", "violet"),
    "lilac": ("haze", "lilac", "violet", "violet"),
    "coral": ("cream", "coral", "rose", "rose"),
    "gold": ("cream", "gold", "tan", "tan"),
    "cream": ("white", "cream", "gold", "tan"),
    "sea": ("mint", "sea", "teal", "teal"),
    "teal": ("sea", "teal", "teal", "ink"),
    "white": ("white", "white", "cream", "gold"),
}
N4 = ((1, 0), (-1, 0), (0, 1), (0, -1))
N8 = N4 + ((1, 1), (-1, -1), (1, -1), (-1, 1))


def spans(rows, top, cx=16):
    """Mask from rows of half-widths (symmetric about cx) or explicit (left, right) spans."""
    mask = set()
    for i, row in enumerate(rows):
        left, right = (cx - row, cx - 1 + row) if isinstance(row, int) else row
        mask.update((x, top + i) for x in range(left, right + 1))
    return mask


def disc(cx, cy, r):
    """Pixel disc centred on a pixel corner when cx/cy end in .5."""
    return {(x, y) for y in range(int(cy - r) - 1, int(cy + r) + 2)
            for x in range(int(cx - r) - 1, int(cx + r) + 2)
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r}


def paint(sprite, points, color):
    for p in points:
        sprite[p] = color


def outline(sprite, color="ink", size=32):
    """Closed 1px outline outside the silhouette.

    Ink goes on the 4-neighbours of the fill, so no fill pixel touches transparency, while the
    outline itself steps diagonally (8-connected) with no doubled corners, as the style guide asks.
    """
    ring = {(x + dx, y + dy) for (x, y), c in sprite.items() if c != color for dx, dy in N4} - set(sprite)
    for x, y in ring:
        if 0 <= x < size and 0 <= y < size:
            sprite[(x, y)] = color
    return sprite


def shade(sprite, mask, ramp, light_rim=True, deep_rows=1, shadow_rows=2):
    """Top-left light: lit rim on upper-left edges, shadow band on lower/right edges.

    Rim highlights only survive in runs of two or more so they never read as specks.
    """
    light, base, shadow, deep = RAMPS[ramp]
    xs = [p[0] for p in mask]
    lit_until = min(xs) + (max(xs) - min(xs)) * 2 // 5
    rim = {(x, y) for x, y in mask if light_rim and x <= lit_until
           and (x, y - 1) not in mask and (x - 1, y) in mask and (x, y + shadow_rows) in mask}
    rim = {(x, y) for x, y in rim if (x - 1, y) in rim or (x + 1, y) in rim}
    for x, y in mask:
        below = sum((x, y + k) in mask for k in range(1, shadow_rows + 1))
        if deep_rows and (x, y + 1) not in mask:
            color = deep
        elif below < shadow_rows or (x + 1, y) not in mask and (x, y - 2) in mask:
            color = shadow
        else:
            color = light if (x, y) in rim else base
        sprite[(x, y)] = color
    return settle(sprite, mask, (light, shadow, deep), base)


def settle(sprite, points, tones, base):
    """Return any shading pixel that has no same-tone 8-neighbour to the base colour."""
    lonely = [p for p in points if sprite.get(p) in tones and sprite[p] != base
              and not any(sprite.get((p[0] + dx, p[1] + dy)) == sprite[p] for dx, dy in N8)]
    for p in lonely:
        sprite[p] = base
    return sprite


def despeck(sprite, keep=("white", "ink", "cream")):
    """Replace single pixels whose colour has no 8-neighbour match with the local majority."""
    out = dict(sprite)
    for (x, y), c in sprite.items():
        if c in keep:
            continue
        near = [sprite.get((x + dx, y + dy)) for dx, dy in N8]
        if c not in near:
            votes = [n for n in near if n and n != "ink"]
            if votes:
                # max() keeps the first of equal counts, so ties go to the earliest N8 neighbour.
                # A set here would iterate in string-hash order, which changes per process.
                out[(x, y)] = max(votes, key=votes.count)
    return out


def to_image(sprite, size=(32, 32)):
    """Palette names or raw '#rrggbb' custom colours to an RGBA image."""
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    for (x, y), c in sprite.items():
        if 0 <= x < size[0] and 0 <= y < size[1]:
            image.putpixel((x, y), tuple(bytes.fromhex(PALETTE.get(c, c)[1:])) + (255,))
    return image


def from_image(image):
    """Opaque pixels as palette names; off-palette colours stay as '#rrggbb'."""
    names = {tuple(bytes.fromhex(v[1:])): k for k, v in PALETTE.items()}
    out = {}
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            if a:
                out[(x, y)] = names.get((r, g, b), f"#{r:02x}{g:02x}{b:02x}")
    return out
