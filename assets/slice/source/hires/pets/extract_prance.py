# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Extract Loki Agent's prancing unicorn from its generated pose sheet.

From the repository root:

    ./scripts/uv run --python 3.12 assets/slice/source/hires/pets/extract_prance.py
    ./scripts/uv run --python 3.12 assets/slice/source/hires/pets/extract_prance.py --check

The input is unicorn-prance-sheet.jpg (its prompt record is beside it): eight side
views of the pet unicorn, facing right. The output is eight RGBA PNGs,
`assets/slice/hires/agent/unicorn-<pose>.png`, each 192x192 (32 logical pixels at
density 6) like the pet frames, keyed with extract_pets.py's pipeline (its steps 1-5
and its single Lanczos resample), so they match the pet unicorn's edges.

What differs from the pet frames is placement, because these frames animate as a
sequence rather than standing alone:

- One scale for all eight: the standing pose is as tall as the pet unicorn's idle-a,
  so the two sets can follow each other.
- Horizontally, each pose's opaque centroid sits on x = 96, so the body stays put
  while legs, mane and tail move.
- Vertically, poses with a hoof on the ground stand their coverage on row 179, like
  the pets; the leap keeps its height above its row's ground from the sheet.

Deterministic like extract_pets.py; --check fails if a committed PNG differs.
"""
import argparse
import statistics
import sys
from fractions import Fraction
from pathlib import Path

from PIL import Image

from extract_pets import (COVER_ALPHA, FRAME, GROUND_ROW, HERE, PAD, REPO, alpha_bounds,
                          cell_crop, key_sheet, png_bytes, resample)

SHEET = "unicorn-prance-sheet.jpg"
OUT_DIR = REPO / "assets/slice/hires/agent"
PET_IDLE = REPO / "assets/slice/hires/creatures/unicorn-idle-a.png"
POSES = ("prance-a", "pass-a", "prance-b", "pass-b", "leap", "land", "rear", "stand")  # cells 0..7
AIRBORNE = {"leap"}
MARGIN = 2  # opaque pixels stay this far inside the frame


def centroid_x(im):
    a = im.getchannel("A").tobytes()
    w = im.width
    total = sum(v for v in a if v >= COVER_ALPHA)
    return Fraction(sum((i % w) * v for i, v in enumerate(a) if v >= COVER_ALPHA), total)


def extract():
    keyed = key_sheet(HERE / SHEET, "green")
    crops = {pose: cell_crop(keyed, cell, SHEET) for cell, pose in enumerate(POSES)}
    boxes = {pose: keyed.cells[cell] for cell, pose in enumerate(POSES)}
    _, top, _, bottom = alpha_bounds(Image.open(PET_IDLE).convert("RGBA"), 1)
    stand = boxes["stand"]
    scale = Fraction(bottom - top + 1, stand[3] - stand[1])
    # The leap's height above ground: its row's grounded poses set the ground line.
    row = [boxes[p][3] for p in ("land", "rear", "stand")]
    lift = statistics.median_low(row) - boxes["leap"][3]
    frames = {}
    for pose in POSES:
        sprite = resample(crops[pose], scale, scale)
        _, _, _, cover_bottom = alpha_bounds(sprite, COVER_ALPHA)
        ground = GROUND_ROW - (round(lift * scale) if pose in AIRBORNE else 0)
        ox = 96 - round(centroid_x(sprite))
        oy = ground - cover_bottom
        x0, y0, x1, y1 = alpha_bounds(sprite, 1)
        if ox + x0 < MARGIN or oy + y0 < MARGIN or ox + x1 >= FRAME - MARGIN or oy + y1 >= FRAME:
            raise ValueError(f"{pose}: placed x {ox + x0}..{ox + x1} y {oy + y0}..{oy + y1} leaves the frame")
        frame = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
        frame.paste(sprite, (ox, oy))
        frames[pose] = frame
    return frames, scale


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed PNGs differ from a fresh extraction")
    args = parser.parse_args()
    frames, scale = extract()
    print(f"unicorn prance: scale {scale} ({float(scale):.4f})")
    stale = []
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for pose, im in frames.items():
        data = png_bytes(im)
        path = OUT_DIR / f"unicorn-{pose}.png"
        x0, y0, x1, y1 = alpha_bounds(im, 1)
        print(f"  {pose:8s} alpha>0 x {x0:3d}..{x1:3d} y {y0:3d}..{y1:3d}")
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(path.name)
        else:
            path.write_bytes(data)
    if stale:
        print("Stale or missing: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
