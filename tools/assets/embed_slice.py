#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Emit immutable C pixel, mask and alpha arrays for the portable renderer.

Every array is `static const`, so ESP32 builds place the art in flash rodata.
Legacy assets carry RGB565 + a 1-bit mask; high-resolution assets carry stored
RGB565 + 8-bit straight alpha, their density, and no mask.
"""
import argparse
from pathlib import Path

from build_slice import AGENT_ONLY, SOURCE, density_of, is_hires, load_assets, pack_mask, runtime_planes

ROOT = Path(__file__).resolve().parents[2]


def emit_array(name, ctype, values, columns=12):
    width = 2 if ctype == "uint8_t" else 4
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for offset in range(0, len(values), columns):
        chunk = values[offset:offset + columns]
        lines.append("    " + ", ".join(f"0x{value:0{width}x}u" for value in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def font_glyphs(image):
    payload = bytearray()
    for index in range(96):
        gx = (index % 16) * 8
        gy = (index // 16) * 12
        payload.extend(pack_mask(image.crop((gx, gy, gx + 8, gy + 12))))
    return payload


def descriptor(asset, pixels, mask, stride, alpha):
    """One positional JelliAsset initializer (field order of include/jelli/assets.h)."""
    ground = asset.get("ground_anchor_q8", [0, 0])
    fields = [f"{asset['id']}u", f"{asset['width']}u", f"{asset['height']}u", pixels, mask, f"{stride}u",
              *(f"{v}u" for v in (*ground, *asset["centroid_q8"], *asset["bounds"])),
              f"{density_of(asset)}u", alpha]
    return "    {" + ", ".join(fields) + "},"


def selected(asset, keys):
    """Whether an asset is embedded: without key prefixes every asset but the agent's
    own art (the pet's set), else only matching keys. The font always is, because
    jelli_asset_glyph reads it."""
    if not keys:
        return asset["kind"] not in AGENT_ONLY
    return asset["kind"] == "font" or any(asset["key"].startswith(key) for key in keys)


def generate(output, source=SOURCE, keys=()):
    manifest, images = load_assets(source)
    chunks = ['#include "jelli/assets.h"', ""]
    records = []
    glyph_data = None
    for asset in manifest["assets"]:
        if not selected(asset, keys):
            continue
        image = images[asset["key"]]
        name = "asset_" + str(asset["id"])
        if asset["kind"] == "font":
            glyph_data = font_glyphs(image)
            records.append((asset["id"], descriptor(asset, "0", "0", 0, "0")))
            chunks.append(emit_array("font_glyphs", "uint8_t", list(glyph_data), 16))
            continue
        planes = runtime_planes(asset, image)
        chunks.append(emit_array(name + "_pixels", "uint16_t", planes["pixels"], 10))
        if is_hires(asset):
            chunks.append(emit_array(name + "_alpha", "uint8_t", list(planes["alpha"]), 16))
            records.append((asset["id"], descriptor(asset, name + "_pixels", "0", 0, name + "_alpha")))
        else:
            stride = (image.width + 7) // 8
            chunks.append(emit_array(name + "_mask", "uint8_t", list(planes["mask"]), 12))
            records.append((asset["id"], descriptor(asset, name + "_pixels", name + "_mask", stride, "0")))
    if glyph_data is None:
        raise ValueError("Font asset is missing")
    chunks.append("static const JelliAsset assets[] = {")
    chunks.extend(line for _, line in sorted(records))
    chunks.extend(["};", "", "const JelliAsset *jelli_asset_find(uint32_t id)", "{", "    for (unsigned i = 0u; i < sizeof(assets) / sizeof(assets[0]); ++i) {", "        if (assets[i].id == id)", "            return &assets[i];", "    }", "    return 0;", "}", "", "const uint8_t *jelli_asset_glyph(uint8_t codepoint)", "{", "    if (codepoint < 32u || codepoint > 127u)", "        codepoint = (uint8_t)'?';", "    return &font_glyphs[(unsigned)(codepoint - 32u) * 12u];", "}", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n\n".join(chunks))
    if keys and len(records) == 1:
        raise ValueError(f"No asset key starts with any of: {', '.join(keys)}")
    hires = sum(1 for asset in manifest["assets"] if is_hires(asset) and selected(asset, keys))
    print(f"Embedded {len(records)} assets ({hires} high-resolution): {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=SOURCE,
                        help="Asset directory containing assets.json (default: assets/slice)")
    parser.add_argument("--keys", nargs="+", default=(), metavar="PREFIX",
                        help="Embed only assets whose key starts with a prefix (and the font)")
    args = parser.parse_args()
    generate(args.output.resolve(), args.source.resolve(), tuple(args.keys))


if __name__ == "__main__":
    main()
