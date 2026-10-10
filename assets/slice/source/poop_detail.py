#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Hand-placed detail for the reduced poop mess (props/poop-1.png, props/poop-2.png).

Run after tools/assets/import_creature.py assets/slice/source/poop-import.json, which
reduces the creature-scale source art. The grids keep the imported silhouette and add three
readable tiers (left highlight, right shading, ledge shadows), a highlight that shifts
between frames for the shimmer, and two alternating stink wisps. Repaint in Jelli Art.
"""
import json
from pathlib import Path

from PIL import Image

SOURCE = Path(__file__).resolve().parents[1]
COLOURS = {"#": "#000000", "b": "#b55838", "d": "#8f4329", "h": "#dfa08a", "s": "#c9d6a3"}
FRAMES = {
    "poop-1": [
        "................",
        "....s......s....",
        "...s....#...s...",
        "....s...##.s....",
        ".......#hb#.....",
        "......#hbbd#....",
        "......#dddd#....",
        "......#hhbd##...",
        ".....#hbbbbbd#..",
        "....#hbbbbbbdd#.",
        ".....#dddddddd#.",
        "...##hhbbbbbbd#.",
        "..#hbbbbbbbbbd#.",
        "..#bbbbbbbbddd#.",
        "..#############.",
        "................",
    ],
    "poop-2": [
        "...s......s.....",
        "....s......s....",
        "....s...#..s....",
        "...s....##..s...",
        ".......#bh#.....",
        "......#bhbd#....",
        "......#dddd#....",
        "......#bhhd##...",
        ".....#bhbbbbd#..",
        "....#bbhbbbbdd#.",
        ".....#dddddddd#.",
        "...##bhhbbbbbd#.",
        "..#bbhbbbbbbbd#.",
        "..#bbbbbbbbddd#.",
        "..#############.",
        "................",
    ],
}


def main():
    manifest_path = SOURCE / "assets.json"
    manifest = json.loads(manifest_path.read_text())
    for name, rows in FRAMES.items():
        assert len(rows) == 16 and all(len(r) == 16 for r in rows), name
        image = Image.new("RGBA", (16, 16))
        for y, row in enumerate(rows):
            for x, key in enumerate(row):
                if key != ".":
                    rgb = tuple(bytes.fromhex(COLOURS[key][1:]))
                    image.putpixel((x, y), rgb + (255,))
        image.save(SOURCE / "props" / f"{name}.png")
        for asset in manifest["assets"]:
            if asset["key"] == f"props.{name}":
                asset["bounds"] = list(image.getchannel("A").getbbox())
    manifest["palettes"]["poop"] = list(COLOURS.values())
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print("Detailed props/poop-1.png and props/poop-2.png")


if __name__ == "__main__":
    main()
