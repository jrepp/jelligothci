# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Validate slice PNGs and emit a standalone preview, contact sheet and raw pixels."""
import argparse
import base64
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw
from sprite_geometry import ground_anchor_q8, opaque_centroid_q8

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "assets/slice"


# Creature frames may be 32x32 or 48x48 (ADR-012); other kinds have one size.
FIXED_SIZES = {"icons": (16, 16), "props": (24, 24), "font": (128, 72), "menus": (32, 32), "meters": (32, 32),
               "health": (32, 32), "effects": (16, 16), "backgrounds": (64, 64), "prizes": (32, 32)}
CREATURE_SIZES = {(32, 32), (48, 48)}
# Runtime pose order; must match JelliCreaturePose in include/jelli/creature.h.
CREATURE_POSES = ("idle", "idle-alt", "curious", "content", "eating", "happy", "asleep", "unwell")
CLIP_FRAME_CAP = 6
PIXEL_BYTES = 215488
PACK_CEILING = 229376


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pack_mask(image):
    """Row-major, MSB-first, each row padded to a whole byte."""
    data = bytearray()
    alpha = image.getchannel("A")
    for y in range(image.height):
        for x in range(0, image.width, 8):
            value = 0
            for bit in range(8):
                if x + bit < image.width and alpha.getpixel((x + bit, y)):
                    value |= 1 << (7 - bit)
            data.append(value)
    return bytes(data)


def palette_colours(manifest, asset):
    """An asset's palette: the shared one, an inline list, or a name in manifest["palettes"]."""
    name = asset.get("palette")
    if name is None:
        return manifest["palette"]
    if isinstance(name, list):
        return name
    palettes = manifest.get("palettes", {})
    require(isinstance(name, str) and name in palettes, f"Unknown palette: {asset['key']}")
    return palettes[name]


def check_creature_clips(manifest):
    """Every creature form needs one clip per runtime pose, keyed "<form>.<pose>"."""
    require(tuple(manifest.get("creature_poses", ())) == CREATURE_POSES, "Creature pose list mismatch")
    forms = {a["form"] for a in manifest["assets"] if a["kind"] == "creatures"}
    keys = {clip["key"] for clip in manifest["clips"]}
    require(keys == {f"{form}.{pose}" for form in forms for pose in CREATURE_POSES}, "Creature clip coverage mismatch")
    for clip in manifest["clips"]:
        form = clip["key"].split(".", 1)[0]
        assets = {a["key"]: a for a in manifest["assets"]}
        require(all(assets[k].get("form") == form for k in clip["frames"]), f"Clip mixes forms: {clip['key']}")


def load_assets():
    manifest = json.loads((SOURCE / "assets.json").read_text())
    require(manifest["schema_version"] == 1, "Unknown manifest schema")
    palette = {tuple(bytes.fromhex(c[1:])) for c in manifest["palette"]}
    require(len(palette) <= 16, "Palette exceeds 16 opaque colors")
    images, ids, paths = {}, set(), set()
    counts = {"creatures": 0, "icons": 0, "props": 0, "font": 0, "menus": 0, "meters": 0, "health": 0, "effects": 0, "backgrounds": 0, "prizes": 0}
    expected = {kind: {size} for kind, size in FIXED_SIZES.items()}
    expected["creatures"] = CREATURE_SIZES
    for asset in manifest["assets"]:
        key, ident, path = asset["key"], asset["id"], asset["path"]
        require(key not in images and ident not in ids and path not in paths, f"Duplicate asset: {key}")
        require(isinstance(ident, int) and 0 < ident < 2**32, f"Invalid ID: {key}")
        full = (SOURCE / path).resolve()
        require(SOURCE.resolve() in full.parents, f"Path escapes source root: {path}")
        image = Image.open(full).convert("RGBA")
        require(image.size in expected[asset["kind"]], f"Wrong dimensions: {key}")
        require(image.size == (asset["width"], asset["height"]), f"Manifest dimensions: {key}")
        require(set(image.getchannel("A").getdata()) <= {0, 255}, f"Nonbinary alpha: {key}")
        asset_palette = {tuple(bytes.fromhex(c[1:])) for c in palette_colours(manifest, asset)}
        require(len(asset_palette) <= 16, f"Palette exceeds 16 colors: {key}")
        require(all((r, g, b) in asset_palette for r, g, b, a in image.getdata() if a), f"Off-palette pixel: {key}")
        require(list(image.getchannel("A").getbbox()) == asset["bounds"], f"Bounds mismatch: {key}")
        asset["centroid_q8"] = opaque_centroid_q8(image)
        if asset["kind"] == "creatures":
            asset["ground_anchor_q8"] = ground_anchor_q8(image)
        require(all(0 <= v < image.size[i] for i, v in enumerate(asset["pivot"])), f"Invalid pivot: {key}")
        if asset["kind"] == "font":
            require((asset["glyph_width"], asset["glyph_height"], asset["glyph_count"], asset["first_codepoint"], asset["columns"]) == (8, 12, 96, 32, 16), "Font layout mismatch")
            for code in range(33, 128):
                x, y = ((code - 32) % 16) * 8, ((code - 32) // 16) * 12
                require(image.crop((x, y, x + 8, y + 12)).getchannel("A").getbbox(), f"Empty glyph: {code}")
        ids.add(ident)
        paths.add(path)
        counts[asset["kind"]] += 1
        images[key] = image
    require(counts == {"creatures": 29, "icons": 12, "props": 4, "font": 1, "menus": 15, "meters": 5, "health": 9, "effects": 8, "backgrounds": 2, "prizes": 9}, "Incomplete slice inventory")
    prize_pixels = {image.tobytes() for key, image in images.items() if key.startswith("prizes.")}
    require(len(prize_pixels) == 9, "Collectible prizes must have nine distinct pixel designs")
    clip_keys = set()
    for clip in manifest["clips"]:
        require(clip["id"] not in ids and clip["key"] not in clip_keys, "Duplicate clip")
        ids.add(clip["id"])
        clip_keys.add(clip["key"])
        require(0 < len(clip["frames"]) <= CLIP_FRAME_CAP, "Clip frame cap")
        require(len(clip["frames"]) == len(clip["durations_ms"]), "Clip timing mismatch")
        require(all(k in images for k in clip["frames"]), "Missing clip frame")
        require(all(isinstance(n, int) and 0 < n <= 10000 for n in clip["durations_ms"]), "Clip duration out of bounds")
        require(isinstance(clip.get("loop"), bool), f"Clip loop flag: {clip['key']}")
    check_creature_clips(manifest)
    return manifest, images


def export_pixels(output, manifest, images):
    folder = output / "pixels"
    folder.mkdir(exist_ok=True)
    records = []
    for asset in manifest["assets"]:
        image = images[asset["key"]]
        name = asset["key"].replace(".", "-")
        if asset["kind"] == "font":
            # Glyph-major, 12 rows of one byte per 8x12 glyph.
            bits = bytearray()
            for n in range(96):
                x, y = (n % 16) * 8, (n // 16) * 12
                bits.extend(pack_mask(image.crop((x, y, x + 8, y + 12))))
            payloads = {f"{name}.bits": bytes(bits)}
        else:
            pixels = b"".join(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                              for r, g, b, a in image.getdata())
            payloads = {f"{name}.rgb565": pixels, f"{name}.mask": pack_mask(image)}
        for filename, data in payloads.items():
            (folder / filename).write_bytes(data)
        records.append({"id": asset["id"], "key": asset["key"], "bytes": sum(map(len, payloads.values())),
                        **({"ground_anchor_q8": asset["ground_anchor_q8"]} if asset["kind"] == "creatures" else {}),
                        "files": {k: {"bytes": len(v), "sha256": hashlib.sha256(v).hexdigest()} for k, v in payloads.items()}})
    total = sum(r["bytes"] for r in records)
    require(total == PIXEL_BYTES, f"Unexpected pixel payload: {total}")
    require(total + 8192 + 4096 <= PACK_CEILING, "Art exceeds the planned pack budget")
    report = {"pixel_bytes": total, "definition_allowance": 8192, "metadata_allowance": 4096,
              "planned_pack_bytes": total + 8192 + 4096, "pack_ceiling": PACK_CEILING,
              "note": "Raw pixels are real exports; definitions, metadata and pack assembly remain allowances, not a compiled game pack.", "assets": records}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def preview(output, manifest, images, report):
    for asset in manifest["assets"]:
        raw = (SOURCE / asset["path"]).read_bytes()
        asset["data_uri"] = "data:image/png;base64," + base64.b64encode(raw).decode()
    payload = json.dumps({"manifest": manifest, "report": report}).replace("<", "\\u003c")
    template = (Path(__file__).with_name("preview.html")).read_text()
    require(template.count("__SLICE_DATA__") == 1, "Preview template data marker mismatch")
    (output / "preview.html").write_text(template.replace("__SLICE_DATA__", payload))
    sheet = Image.new("RGB", (960, ((len(manifest["assets"]) + 5) // 6) * 150 + 30), "#f5efdf")
    draw = ImageDraw.Draw(sheet)
    for i, asset in enumerate(manifest["assets"]):
        x, y = (i % 6) * 160, (i // 6) * 150
        image = images[asset["key"]]
        scale = 1 if asset["kind"] in ("font", "backgrounds") else 3
        sprite = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
        if asset["kind"] == "font":
            draw.rectangle((x + 8, y + 4, x + 151, y + 87), fill="#49334f")
        sheet.paste(sprite, (x + (160 - sprite.width) // 2, y + 8), sprite)
        draw.text((x + 5, y + 110), asset["key"].split(".", 1)[1], fill="#291b35")
        draw.text((x + 5, y + 126), f'{asset["width"]} x {asset["height"]} / {asset["id"]}', fill="#72516b")
    sheet.save(output / "contact-sheet.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=REPO / "build/assets")
    args = parser.parse_args()
    output = args.output.resolve()
    require(output != SOURCE and SOURCE not in output.parents, "Do not export over source assets")
    output.mkdir(parents=True, exist_ok=True)
    manifest, images = load_assets()
    report = export_pixels(output, manifest, images)
    preview(output, manifest, images, report)
    print(f'Validated {len(images)} assets; {report["pixel_bytes"]:,} raw bytes. Preview: {output / "preview.html"}')


if __name__ == "__main__":
    main()
