#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Two-frame broom (props/broom-1.png, props/broom-2.png) that sweeps a mess away.

16x16 like the reduced poop, so both draw at the pet's pixel scale. Frame 2 mirrors
frame 1, so alternating them reads as a back-and-forth swish. Shared 16-colour palette.
"""
import json
from pathlib import Path

from PIL import Image

SOURCE = Path(__file__).resolve().parents[1]
KEYS = {"#": 0, "w": 15, "c": 12, "g": 14, "y": 11}  # ink, wood (also bristle shadow), red tie, straw, white glint
BROOM = [
    "................",
    ".##.............",
    ".#w#............",
    "..#w#...........",
    "...#w#..........",
    "....#w#.........",
    ".....#w#........",
    "......#w#.......",
    ".....##cc##.....",
    ".....#####......",
    "....#yyggyg#....",
    "...#gwggwggw#...",
    "...#gwggwggw#...",
    "..#ggwggwggwg#..",
    "..#gwggwggwgww#.",
    "..#############.",
]


def draw(rows, palette):
    image = Image.new("RGBA", (16, 16))
    for y, row in enumerate(rows):
        assert len(row) == 16, (y, row)
        for x, key in enumerate(row):
            if key != ".":
                image.putpixel((x, y), palette[KEYS[key]] + (255,))
    return image


def main():
    manifest = json.loads((SOURCE / "assets.json").read_text())
    palette = [tuple(bytes.fromhex(c[1:])) for c in manifest["palette"]]
    assert len(BROOM) == 16
    left = draw(BROOM, palette)
    left.save(SOURCE / "props/broom-1.png")
    left.transpose(Image.Transpose.FLIP_LEFT_RIGHT).save(SOURCE / "props/broom-2.png")
    print("Wrote props/broom-1.png and props/broom-2.png")


if __name__ == "__main__":
    main()
