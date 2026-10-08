#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Emit immutable C pixel and mask arrays for the portable renderer."""
import argparse
from pathlib import Path

from build_slice import load_assets

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/slice"


def mask_rows(image):
    alpha = image.getchannel("A")
    stride = (image.width + 7) // 8
    output = bytearray()
    for y in range(image.height):
        for byte_x in range(stride):
            bits = 0
            for bit in range(8):
                x = byte_x * 8 + bit
                if x < image.width and alpha.getpixel((x, y)):
                    bits |= 1 << (7 - bit)
            output.append(bits)
    return output, stride


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def emit_array(name, ctype, values, columns=12):
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for offset in range(0, len(values), columns):
        chunk = values[offset:offset + columns]
        lines.append("    " + ", ".join(f"0x{value:02x}u" if ctype == "uint8_t" else f"0x{value:04x}u" for value in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def generate(output):
    manifest, images = load_assets()
    chunks = ['#include "jelli/assets.h"', ""]
    records = []
    glyph_data = None
    for asset in manifest["assets"]:
        image = images[asset["key"]]
        name = "asset_" + str(asset["id"])
        if asset["kind"] == "font":
            payload = bytearray()
            for index in range(96):
                gx = (index % 16) * 8
                gy = (index // 16) * 12
                glyph, _ = mask_rows(image.crop((gx, gy, gx + 8, gy + 12)))
                payload.extend(glyph)
            glyph_data = payload
            records.append((asset["id"], asset["width"], asset["height"], None, 0, 0, 0, *asset["centroid_q8"], *asset["bounds"]))
            chunks.append(emit_array("font_glyphs", "uint8_t", list(payload), 16))
            continue
        rgba = list(image.getdata())
        pixels = [rgb565(r, g, b) for r, g, b, a in rgba]
        mask, stride = mask_rows(image)
        chunks.append(emit_array(name + "_pixels", "uint16_t", pixels, 10))
        chunks.append(emit_array(name + "_mask", "uint8_t", list(mask), 12))
        records.append((asset["id"], asset["width"], asset["height"], name, stride, *asset.get("ground_anchor_q8", [0, 0]), *asset["centroid_q8"], *asset["bounds"]))
    chunks.append("static const JelliAsset assets[] = {")
    for ident, width, height, name, stride, ground_x, ground_y, center_x, center_y, left, top, right, bottom in sorted(records):
        if name is None:
            chunks.append(f"    {{{ident}u, {width}u, {height}u, 0, 0, 0u, 0u, 0u, {center_x}u, {center_y}u, {left}u, {top}u, {right}u, {bottom}u}},")
        else:
            chunks.append(f"    {{{ident}u, {width}u, {height}u, {name}_pixels, {name}_mask, {stride}u, {ground_x}u, {ground_y}u, {center_x}u, {center_y}u, {left}u, {top}u, {right}u, {bottom}u}},")
    chunks.extend(["};", "", "const JelliAsset *jelli_asset_find(uint32_t id)", "{", "    for (unsigned i = 0u; i < sizeof(assets) / sizeof(assets[0]); ++i) {", "        if (assets[i].id == id)", "            return &assets[i];", "    }", "    return 0;", "}", "", "const uint8_t *jelli_asset_glyph(uint8_t codepoint)", "{", "    if (codepoint < 32u || codepoint > 127u)", "        codepoint = (uint8_t)'?';", "    return &font_glyphs[(unsigned)(codepoint - 32u) * 12u];", "}", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n\n".join(chunks))
    if glyph_data is None:
        raise ValueError("Font asset is missing")
    print(f"Embedded {len(records)} assets: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.output.resolve())


if __name__ == "__main__":
    main()
