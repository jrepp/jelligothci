# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Explicit, simultaneous RGB substitutions for authored character variants."""
import argparse
import base64
import json
from pathlib import Path
import re

from PIL import Image
import paint_layers


def validate_mapping(mapping, palette=None):
    """Reject typos instead of silently producing an unchanged variant."""
    if not isinstance(mapping, dict) or not mapping:
        raise ValueError("Palette swap needs a nonempty source-to-target color map")
    if any(not isinstance(c, str) or not re.fullmatch(r"#[0-9a-f]{6}", c)
           for pair in mapping.items() for c in pair):
        raise ValueError("Palette swap colors must be lowercase #rrggbb")
    if palette is not None and not set(mapping).issubset(palette):
        raise ValueError("Palette swap source colors must belong to the template palette")
    if all(source == target for source, target in mapping.items()):
        raise ValueError("Palette swap must change at least one color")
    return mapping


def remap_image(image, mapping):
    """Keep geometry and alpha exactly; each pixel is looked up only once."""
    validate_mapping(mapping)
    document = paint_layers.read(image)
    lookup = {tuple(bytes.fromhex(a[1:])): tuple(bytes.fromhex(b[1:]))
              for a, b in mapping.items()}
    result = image.convert("RGBA")
    result.putdata([(*lookup.get(pixel[:3], pixel[:3]), pixel[3]) if pixel[3] else pixel
                    for pixel in result.getdata()])
    if document:
        for layer in document["layers"]:
            pixels = Image.frombytes("RGBA", image.size, base64.b64decode(layer["data"]))
            layer["data"] = base64.b64encode(remap_image(pixels, mapping).tobytes()).decode("ascii")
        result.info[paint_layers.KEY] = json.dumps(paint_layers.validate(document, result), separators=(",", ":"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--map", required=True, type=Path, dest="mapping")
    args = parser.parse_args()
    mapping = json.loads(args.mapping.read_text())
    with Image.open(args.source) as image:
        image = image.convert("RGBA")
        colors = {"#%02x%02x%02x" % p[:3] for p in image.getdata() if p[3]}
        validate_mapping(mapping, colors)
        result = remap_image(image, mapping)
    # Never overwrite a source or an existing authored result.
    with args.output.open("xb") as output:
        result.save(output, format="PNG")


if __name__ == "__main__":
    main()
