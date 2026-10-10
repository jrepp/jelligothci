#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Placeholder open-book art for the Reading activity (shared 16-colour palette).

Writes menus/reading.png (32x32 ring icon) and props/book.png (24x24 prop held
while reading). Repaint freely in Jelli Art; this recipe is only the starting point.
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw

SOURCE = Path(__file__).resolve().parents[1]


def colours():
    palette = json.loads((SOURCE / "assets.json").read_text())["palette"]
    c = [tuple(bytes.fromhex(h[1:])) for h in palette]
    return {"ink": c[0], "shadow": c[1], "teal": c[3], "lilac": c[8], "lilac_light": c[9],
            "cream": c[10], "white": c[11], "coral": c[13], "gold": c[14]}


def open_book(size, k):
    """Two pages over a coral cover, an ink spine, and lilac text lines."""
    image = Image.new("RGBA", (size, size))
    d = ImageDraw.Draw(image)
    s = size / 32
    p = lambda pts: [(round(x * s), round(y * s)) for x, y in pts]  # noqa: E731
    cover = p([(1, 11), (15, 13), (16, 13), (31, 11), (31, 27), (16, 29), (15, 29), (1, 27)])
    d.polygon(cover, fill=k["ink"])
    d.polygon(p([(2, 12), (15, 14), (15, 28), (2, 26)]), fill=k["coral"])
    d.polygon(p([(30, 12), (16, 14), (16, 28), (30, 26)]), fill=k["coral"])
    d.polygon(p([(3, 9), (15, 12), (15, 26), (3, 23)]), fill=k["ink"])
    d.polygon(p([(29, 9), (16, 12), (16, 26), (29, 23)]), fill=k["ink"])
    d.polygon(p([(4, 10), (14, 13), (14, 25), (4, 22)]), fill=k["cream"])
    d.polygon(p([(28, 10), (17, 13), (17, 25), (28, 22)]), fill=k["white"])
    for row in range(3):
        y = 14 + row * 3
        d.line(p([(6, y), (12, y + 1)]), fill=k["lilac"], width=1)
        d.line(p([(19, y + 1), (26, y)]), fill=k["lilac_light"], width=1)
    d.line(p([(15, 12), (15, 27)]), fill=k["shadow"], width=max(1, round(s)))
    if size >= 32:  # A small gold ribbon reads as "book", not "card", at ring size.
        d.rectangle((21, 24, 22, 30), fill=k["ink"])
        d.point((21, 25), fill=k["gold"])
        d.point((21, 26), fill=k["gold"])
        d.point((21, 27), fill=k["gold"])
        d.point((21, 28), fill=k["gold"])
    return image


def main():
    k = colours()
    open_book(32, k).save(SOURCE / "menus/reading.png")
    open_book(24, k).save(SOURCE / "props/book.png")
    print("Wrote menus/reading.png and props/book.png")


if __name__ == "__main__":
    main()
