#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Placeholder potty-chair icon for the POTTY routine (32x32, shared palette).

Repaint freely in Jelli Art; this recipe is only the starting point.
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw

SOURCE = Path(__file__).resolve().parents[1]


def main():
    c = [tuple(bytes.fromhex(h[1:])) for h in json.loads((SOURCE / "assets.json").read_text())["palette"]]
    ink, shadow, teal, mint, cream, white, lilac = c[0], c[1], c[3], c[5], c[10], c[11], c[9]
    image = Image.new("RGBA", (32, 32))
    d = ImageDraw.Draw(image)
    d.rounded_rectangle((7, 3, 25, 14), radius=4, fill=ink)        # Lid, tipped up.
    d.rounded_rectangle((9, 5, 23, 12), radius=3, fill=lilac)
    d.line([(11, 7), (16, 6)], fill=white, width=1)
    d.rounded_rectangle((3, 13, 29, 22), radius=4, fill=ink)       # Seat rim.
    d.rounded_rectangle((5, 15, 27, 20), radius=3, fill=white)
    d.ellipse((10, 16, 22, 19), fill=shadow)                       # Opening.
    d.polygon([(7, 21), (25, 21), (23, 29), (9, 29)], fill=ink)     # Base.
    d.polygon([(9, 22), (23, 22), (21, 27), (11, 27)], fill=teal)
    d.line([(11, 23), (20, 23)], fill=mint, width=1)
    d.point((6, 16), fill=cream)
    image.save(SOURCE / "health/potty.png")
    print("Wrote health/potty.png")


if __name__ == "__main__":
    main()
