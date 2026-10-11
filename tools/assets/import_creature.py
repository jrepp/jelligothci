#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Import upscaled pixel-art frames as one creature form, or as another sprite kind.

A spec JSON (see assets/slice/source/axolotl-import.json) names the source
files, the source cell size, the target canvas, and each frame's pose. Every
cell becomes one pixel by majority vote, so stray anti-aliased strokes do not
leak colours. All frames are cropped by their shared union bounds, which keeps
their relative placement, then bottom-centred on the pivot row.

The importer writes PNGs under assets/slice/creatures/ and upserts the frame
assets and the form's named palette in assets.json. Clips are authored data and
are left alone; edit them in the manifest or in Jelli Art. Entries marked
"studio": true are frames added in Jelli Art: they keep their ID slot, have no
source file, and their PNG and asset are left alone.
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


def luminance(colour):
    return 0.299 * colour[0] + 0.587 * colour[1] + 0.114 * colour[2]


def reduce_art(image, width, highlight=0.2, line=0.4):
    """Pixel-art reduction of native art to a target width (for props drawn beside a pet).

    The silhouette follows coverage; the body takes a vote of non-outline colours but keeps
    the lightest colour where it covers `highlight` of a block (shimmer survives); interior
    strokes stay where outline fills `line` of a block; then a fresh 1 px outline is drawn
    around the reduced silhouette, as Jelli Art's Tidy outline does.
    """
    step = image.width / width
    height = max(1, round(image.height / step))
    px = image.load()
    opaque = {p[:3] for p in image.getdata() if p[3]}
    outline, light = min(opaque, key=luminance), max(opaque, key=luminance)
    out = Image.new("RGBA", (width, height))
    for y in range(height):
        for x in range(width):
            xs = range(int(x * step), max(int(x * step) + 1, int((x + 1) * step)))
            ys = range(int(y * step), max(int(y * step) + 1, int((y + 1) * step)))
            block = [px[i, j] for j in ys for i in xs if i < image.width and j < image.height]
            solid = [p[:3] for p in block if p[3]]
            if len(solid) * 2 < len(block):
                continue
            if sum(p == outline for p in solid) >= line * len(block):
                out.putpixel((x, y), outline + (255,))
                continue
            body = Counter(p for p in solid if p != outline) or Counter(solid)
            pick = light if body[light] >= highlight * sum(body.values()) else body.most_common(1)[0][0]
            out.putpixel((x, y), pick + (255,))
    o = out.load()
    edge = [(x, y) for y in range(height) for x in range(width) if o[x, y][3] and any(
        not (0 <= x + dx < width and 0 <= y + dy < height) or not o[x + dx, y + dy][3]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    for point in edge:
        out.putpixel(point, outline + (255,))
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


def palette_of(frames, existing=()):
    """The named palette: existing colours keep their order (ramps and names refer to them, and
    other specs may share it), then any new colours, most used first."""
    colours = Counter("#%02x%02x%02x" % p[:3] for frame in frames for p in frame.getdata() if p[3])
    palette = list(existing) + [c for c, _ in colours.most_common() if c not in existing]
    if len(palette) > 16:
        raise ValueError(f"{len(palette)} colours; sprites allow at most 16")
    return palette


def frame_name(spec, entry):
    return f"{spec.get('form') or spec['name']}-{entry['pose']}"


def upsert(manifest, spec, frames):
    kind = spec.get("kind", "creatures")
    palettes = manifest.setdefault("palettes", {})
    palettes[spec["palette"]] = palette_of(frames, palettes.get(spec["palette"], ()))
    by_key = {asset["key"]: asset for asset in manifest["assets"]}
    imported = iter(frames)
    for index, entry in enumerate(spec["frames"]):
        if entry.get("studio"):  # Added in Jelli Art: keeps its ID slot; the studio owns its PNG and asset.
            continue
        frame = next(imported)
        name = frame_name(spec, entry)
        if entry.get("retired"):  # Keeps its ID slot; the asset and PNG are removed.
            manifest["assets"] = [a for a in manifest["assets"] if a["key"] != f"{kind}.{name}"]
            (SOURCE / kind / f"{name}.png").unlink(missing_ok=True)
            continue
        record = {"id": spec["first_id"] + index, "key": f"{kind}.{name}",
                  "path": f"{kind}/{name}.png", "kind": kind,
                  "width": frame.width, "height": frame.height, "pivot": list(spec["pivot"]),
                  "bounds": list(frame.getchannel("A").getbbox()), "palette": spec["palette"]}
        if kind == "creatures":  # Creature frames carry their form and pose for clips.
            record.update(form=spec["form"], pose=entry["pose"])
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
    # Retired frames still join the union bounds so the remaining frames keep their placement.
    entries = [f for f in spec["frames"] if not f.get("studio")]  # Jelli Art frames have no source file.
    sources = [downsample(Image.open(args.source_dir / f["source"]).convert("RGBA"), cell) for f in entries]
    if spec.get("reduce_to_width"):  # Shared crop first, so every frame reduces identically.
        box = union_bounds(sources)
        sources = [reduce_art(source.crop(box), spec["reduce_to_width"]) for source in sources]
    frames = place(sources, spec["canvas"], spec["pivot"])
    for entry, frame in zip(entries, frames):
        if entry.get("retired"):
            continue
        frame.save(SOURCE / spec.get("kind", "creatures") / f"{frame_name(spec, entry)}.png", optimize=True)
    manifest_path = SOURCE / "assets.json"
    manifest = json.loads(manifest_path.read_text())
    upsert(manifest, spec, frames)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    kept = sum(not entry.get("retired") for entry in entries)
    print(f"Imported {kept} {spec.get('form') or spec['name']} frames ({frames[0].width}x{frames[0].height})")


if __name__ == "__main__":
    main()
