"""Re-ink pass for the code-authored icon families.

Keeps each icon's drawing, then normalises it to the house style: one closed
1px ink outline, no stray specks, no outline thicker than a pixel.
"""
import json

from PIL import Image

from pixel_kit import N4, N8, despeck, from_image, outline

KINDS = ("icons", "menus", "meters", "health", "effects", "prizes")
# Assets owned by other in-flight work or deliberately left as authored.
SKIP = {"menus.exercise", "menus.water"}


def strip_outer_ink(sprite, size):
    """Peel ink that touches transparency until only interior ink remains.

    Ink on the canvas border is kept: edge_ink() put it there, so re-running is a no-op.
    """
    while True:
        edge = [p for p, c in sprite.items() if c == "ink" and 0 < p[0] < size - 1 and 0 < p[1] < size - 1
                and any((p[0] + dx, p[1] + dy) not in sprite for dx, dy in N4)]
        if not edge:
            return sprite
        for p in edge:
            del sprite[p]


def edge_ink(sprite, size):
    """Where art meets the canvas border there is no room outside; ink the edge pixel instead."""
    for (x, y), c in list(sprite.items()):
        if c != "ink" and (x in (0, size - 1) or y in (0, size - 1)):
            sprite[(x, y)] = "ink"
    return sprite


# Fill colour -> 1px drop-shadow tone along the bottom of each silhouette (32px icons only).
DROP = {"mint": "sea", "sea": "teal", "coral": "rose", "gold": "tan", "lilac": "violet", "haze": "lilac"}


def drop_shade(sprite):
    """Shade the lowest pixel of each fill column that sits on the outline, like the creatures."""
    out = dict(sprite)
    for (x, y), c in sprite.items():
        below, above = sprite.get((x, y + 1)), sprite.get((x, y - 1))
        if c in DROP and below is None and above == c:
            out[(x, y)] = DROP[c]
    for (x, y), c in sprite.items():
        tone = out[(x, y)]
        if tone != c and not any(out.get((x + dx, y + dy)) == tone for dx, dy in N8):
            out[(x, y)] = c
    return out


def reink(image):
    size = image.width
    sprite = strip_outer_ink(from_image(image.convert("RGBA")), size)
    sprite = despeck(sprite)
    if size >= 32:
        sprite = drop_shade(sprite)
    return outline(edge_ink(sprite, size), size=size)


def icons(source):
    manifest = json.loads((source / "assets.json").read_text())
    out = {}
    for asset in manifest["assets"]:
        if asset["kind"] in KINDS and asset["key"] not in SKIP:
            out[asset["path"]] = reink(Image.open(source / asset["path"]))
    return out
