#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Import upscaled pixel-art frames as one creature form.

A spec JSON (see assets/slice/source/axolotl-import.json) names the source
files, the source cell size, the target canvas, and each frame's pose. Every
cell becomes one pixel by majority vote, so stray anti-aliased strokes do not
leak colours. All frames are cropped by their shared union bounds, which keeps
their relative placement, then bottom-centred on the pivot row.

The importer writes PNGs under assets/slice/creatures/ and upserts the frame
assets and the form's named palette in assets.json. Clips are authored data and
are left alone; edit them in the manifest or in Jelli Art.
"""
import argparse
import json
from collections import Counter
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "assets/slice"


def downsample(image, cell):
    """One output pixel per source cell; majority of a 4x4 sample grid."""
    width, height = image.width // cell, image.height // cell
    out = Image.new("RGBA", (width, height))
    step = max(1, cell // 4)
    offsets = range(step // 2, cell, step)
    for y in range(height):
        for x in range(width):
            votes = Counter(image.getpixel((x * cell + dx, y * cell + dy)) for dx in offsets for dy in offsets)
            r, g, b, a = votes.most_common(1)[0][0]
            out.putpixel((x, y), (r, g, b, 255) if a >= 128 else (0, 0, 0, 0))
    return out


def union_bounds(images):
    boxes = [image.getchannel("A").getbbox() for image in images]
    if any(box is None for box in boxes):
        raise ValueError("A source frame is empty")
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def place(images, canvas, pivot):
    left, top, right, bottom = union_bounds(images)
    width, height = right - left, bottom - top
    if width > canvas[0] or height > pivot[1]:
        raise ValueError(f"Art {width}x{height} does not fit {canvas} above pivot row {pivot[1]}")
    x = pivot[0] - width // 2
    y = pivot[1] - height
    placed = []
    for image in images:
        frame = Image.new("RGBA", tuple(canvas), (0, 0, 0, 0))
        frame.paste(image.crop((left, top, right, bottom)), (x, y))
        placed.append(frame)
    return placed


def palette_of(frames):
    colours = Counter("#%02x%02x%02x" % p[:3] for frame in frames for p in frame.getdata() if p[3])
    if len(colours) > 16:
        raise ValueError(f"{len(colours)} colours; sprites allow at most 16")
    return [c for c, _ in colours.most_common()]


def upsert(manifest, spec, frames):
    form = spec["form"]
    manifest.setdefault("palettes", {})[spec["palette"]] = palette_of(frames)
    by_key = {asset["key"]: asset for asset in manifest["assets"]}
    for index, (entry, frame) in enumerate(zip(spec["frames"], frames)):
        name = f"{form}-{entry['pose']}"
        record = {"id": spec["first_id"] + index, "key": f"creatures.{name}",
                  "path": f"creatures/{name}.png", "kind": "creatures",
                  "width": frame.width, "height": frame.height, "pivot": list(spec["pivot"]),
                  "bounds": list(frame.getchannel("A").getbbox()), "form": form,
                  "pose": entry["pose"], "palette": spec["palette"]}
        if record["key"] in by_key:
            by_key[record["key"]].update(record)
        else:
            manifest["assets"].append(record)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("spec", type=Path, help="import spec JSON")
    parser.add_argument("source_dir", type=Path, help="directory holding the spec's source PNGs")
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text())
    cell = spec["source_cell"]
    sources = [downsample(Image.open(args.source_dir / f["source"]).convert("RGBA"), cell) for f in spec["frames"]]
    frames = place(sources, spec["canvas"], spec["pivot"])
    for entry, frame in zip(spec["frames"], frames):
        frame.save(SOURCE / "creatures" / f"{spec['form']}-{entry['pose']}.png", optimize=True)
    manifest_path = SOURCE / "assets.json"
    manifest = json.loads(manifest_path.read_text())
    upsert(manifest, spec, frames)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Imported {len(frames)} {spec['form']} frames ({frames[0].width}x{frames[0].height})")


if __name__ == "__main__":
    main()
