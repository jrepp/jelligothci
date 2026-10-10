# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Validate slice PNGs and emit a standalone preview, contact sheet and raw pixels.

Two asset formats coexist in assets.json:

* Legacy pixel art: no `density`; an RGBA PNG of exactly the kind's logical size,
  16-colour palette, binary alpha. Runtime planes: RGB565 + 1-bit mask.
* High resolution: `"density": D` (2..8); an 8-bit RGBA PNG under
  `hires/<kind>/` of exactly logical size x D, any colours, straight alpha.
  Runtime planes: RGB565 + 8-bit alpha. A derived 1-bit mask is exported for
  inspection only. `width`, `height`, `bounds` and `pivot` stay LOGICAL.
"""
import argparse
import base64
import hashlib
import json
import struct
import sys
from array import array
from pathlib import Path

from PIL import Image, ImageDraw
from sprite_geometry import (OPAQUE_ALPHA, ground_anchor_q8, logical_bounds,
                             opaque_centroid_q8)

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "assets/slice"
LOGICAL_SIZES = {"creatures": (32, 32), "icons": (16, 16), "props": (24, 24), "font": (128, 72),
                 "menus": (32, 32), "meters": (32, 32), "health": (32, 32), "effects": (16, 16),
                 "backgrounds": (64, 64), "prizes": (32, 32), "battery": (16, 16), "agent": (32, 32)}
INVENTORY = {"creatures": 32, "icons": 12, "props": 4, "font": 1, "menus": 13, "meters": 5,
             "health": 9, "effects": 8, "backgrounds": 2, "prizes": 9, "battery": 7, "agent": 8}
# Art only Loki Agent embeds (RFC-003): kept out of the pet's firmware and its pack.
AGENT_ONLY = {"agent"}
# Every logical scale the renderer draws each kind at (core/pet_*.c). A density D is
# drawable at scale s when s % D == 0 (1:1 or nearest upscale) or D % s == 0 (box
# downscale). Kinds absent here (props, backgrounds, font) stay legacy-only.
DRAW_SCALES = {"creatures": (6,), "menus": (3, 1), "icons": (6, 2), "meters": (2,),
               "health": (3,), "effects": (1, 2, 3), "prizes": (2,), "battery": (2, 4), "agent": (6,)}
MAX_DENSITY = 8
# Flash budget for embedded art (bytes). The full high-resolution set is about
# 1.65 M stored pixels x 3 bytes (RGB565 + alpha) = 4.96 MB; 6 MiB leaves room for
# one more eight-pose pet inside the 8 MiB factory app partition with ~1 MB of code.
DEFINITION_ALLOWANCE = 8192
METADATA_ALLOWANCE = 4096
PACK_CEILING = 6 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def density_of(asset):
    """Stored pixels per logical pixel; 1 for legacy entries."""
    return asset.get("density", 1)


def is_hires(asset):
    return "density" in asset


def pack_mask(image):
    """Row-major, MSB-first, each row padded to a whole byte; alpha >= 128 is covered."""
    data = bytearray()
    alpha = image.getchannel("A")
    for y in range(image.height):
        for x in range(0, image.width, 8):
            value = 0
            for bit in range(8):
                if x + bit < image.width and alpha.getpixel((x + bit, y)) >= OPAQUE_ALPHA:
                    value |= 1 << (7 - bit)
            data.append(value)
    return bytes(data)


def rgb565_values(image, zero_transparent=False):
    """Stored RGB565 words; high-resolution art zeroes fully transparent colour."""
    return [0 if zero_transparent and a == 0 else ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            for r, g, b, a in image.getdata()]


def little_endian(words):
    data = array("H", words)
    if sys.byteorder != "little":
        data.byteswap()
    return data.tobytes()


def runtime_planes(asset, image):
    """Planes a host embeds or loads: {'pixels': [...], 'mask'|'alpha': bytes}."""
    if is_hires(asset):
        return {"pixels": rgb565_values(image, True), "alpha": bytes(image.getchannel("A").getdata())}
    return {"pixels": rgb565_values(image), "mask": pack_mask(image)}


def validate_density(asset, key):
    density = asset["density"]
    require(type(density) is int and 2 <= density <= MAX_DENSITY, f"Invalid density: {key}")
    scales = DRAW_SCALES.get(asset["kind"])
    require(scales is not None, f"Kind stays legacy-only: {key}")
    require(all(s % density == 0 or density % s == 0 for s in scales),
            f"Density {density} cannot draw at scales {scales}: {key}")
    require(asset["path"].startswith(f"hires/{asset['kind']}/"), f"High-resolution path must be hires/{asset['kind']}/: {key}")
    return density


def open_image(source, asset, key):
    full = (source / asset["path"]).resolve()
    require(source.resolve() in full.parents, f"Path escapes source root: {asset['path']}")
    with Image.open(full) as opened:
        require(not is_hires(asset) or (opened.format, opened.mode) == ("PNG", "RGBA"),
                f"High-resolution art must be an 8-bit RGBA PNG: {key}")
        return opened.convert("RGBA")


def validate_legacy(manifest, asset, image, key):
    require(set(image.getchannel("A").getdata()) <= {0, 255}, f"Nonbinary alpha: {key}")
    asset_palette = {tuple(bytes.fromhex(c[1:])) for c in asset.get("palette", manifest["palette"])}
    require(len(asset_palette) <= 16, f"Palette exceeds 16 colors: {key}")
    require(all((r, g, b) in asset_palette for r, g, b, a in image.getdata() if a), f"Off-palette pixel: {key}")


def validate_asset(source, manifest, asset):
    key = asset["key"]
    density = validate_density(asset, key) if is_hires(asset) else 1
    image = open_image(source, asset, key)
    logical = LOGICAL_SIZES[asset["kind"]]
    require((asset["width"], asset["height"]) == logical, f"Manifest dimensions: {key}")
    require(image.size == (logical[0] * density, logical[1] * density), f"Wrong dimensions: {key}")
    if density == 1:
        validate_legacy(manifest, asset, image, key)
    require(any(a >= OPAQUE_ALPHA for a in image.getchannel("A").getdata()), f"Empty artwork: {key}")
    bounds = logical_bounds(image, density)
    require(bounds == asset["bounds"], f"Bounds mismatch: {key} (art has {bounds})")
    asset["centroid_q8"] = opaque_centroid_q8(image, density)
    if asset["kind"] == "creatures":
        asset["ground_anchor_q8"] = ground_anchor_q8(image, density)
    require(all(0 <= v < logical[i] for i, v in enumerate(asset["pivot"])), f"Invalid pivot: {key}")
    if asset["kind"] == "font":
        require((asset["glyph_width"], asset["glyph_height"], asset["glyph_count"], asset["first_codepoint"], asset["columns"]) == (8, 12, 96, 32, 16), "Font layout mismatch")
        for code in range(33, 128):
            x, y = ((code - 32) % 16) * 8, ((code - 32) // 16) * 12
            require(image.crop((x, y, x + 8, y + 12)).getchannel("A").getbbox(), f"Empty glyph: {code}")
    return image


def load_assets(source=SOURCE, inventory=None):
    """Validate the manifest at `source`; `inventory` defaults to the shipped counts."""
    source = Path(source)
    manifest = json.loads((source / "assets.json").read_text())
    require(manifest["schema_version"] == 1, "Unknown manifest schema")
    palette = {tuple(bytes.fromhex(c[1:])) for c in manifest["palette"]}
    require(len(palette) <= 16, "Palette exceeds 16 opaque colors")
    images, ids, paths = {}, set(), set()
    counts = dict.fromkeys(LOGICAL_SIZES, 0)
    for asset in manifest["assets"]:
        key, ident, path = asset["key"], asset["id"], asset["path"]
        require(key not in images and ident not in ids and path not in paths, f"Duplicate asset: {key}")
        require(isinstance(ident, int) and 0 < ident < 2**32, f"Invalid ID: {key}")
        require(asset["kind"] in LOGICAL_SIZES, f"Unknown kind: {key}")
        images[key] = validate_asset(source, manifest, asset)
        ids.add(ident)
        paths.add(path)
        counts[asset["kind"]] += 1
    require(counts == (INVENTORY if inventory is None else inventory), "Incomplete slice inventory")
    prize_pixels = {image.tobytes() for key, image in images.items() if key.startswith("prizes.")}
    require(len(prize_pixels) == counts["prizes"], "Collectible prizes must have distinct pixel designs")
    clip_keys = set()
    for clip in manifest["clips"]:
        require(clip["id"] not in ids and clip["key"] not in clip_keys, "Duplicate clip")
        ids.add(clip["id"])
        clip_keys.add(clip["key"])
        require(0 < len(clip["frames"]) <= 6, "Clip frame cap")
        require(len(clip["frames"]) == len(clip["durations_ms"]), "Clip timing mismatch")
        require(all(k in images for k in clip["frames"]), "Missing clip frame")
        require(all(isinstance(n, int) and 0 < n <= 10000 for n in clip["durations_ms"]), "Clip duration out of bounds")
    return manifest, images


def asset_exports(asset, image):
    """(runtime payloads, inspection-only payloads) keyed by export filename."""
    name = asset["key"].replace(".", "-")
    if asset["kind"] == "font":
        # Glyph-major, 12 rows of one byte per 8x12 glyph.
        bits = bytearray()
        for n in range(96):
            x, y = (n % 16) * 8, (n // 16) * 12
            bits.extend(pack_mask(image.crop((x, y, x + 8, y + 12))))
        return {f"{name}.bits": bytes(bits)}, {}
    planes = runtime_planes(asset, image)
    runtime = {f"{name}.rgb565": little_endian(planes["pixels"])}
    if is_hires(asset):
        runtime[f"{name}.a8"] = planes["alpha"]
        return runtime, {f"{name}.mask": pack_mask(image)}
    runtime[f"{name}.mask"] = planes["mask"]
    return runtime, {}


def export_pixels(output, manifest, images):
    folder = output / "pixels"
    folder.mkdir(exist_ok=True)
    records = []
    for asset in manifest["assets"]:
        runtime, derived = asset_exports(asset, images[asset["key"]])
        for filename, data in {**runtime, **derived}.items():
            (folder / filename).write_bytes(data)
        density = density_of(asset)
        records.append({"id": asset["id"], "key": asset["key"], "density": density,
                        "stored_size": [asset["width"] * density, asset["height"] * density],
                        "bytes": sum(map(len, runtime.values())),
                        **({"ground_anchor_q8": asset["ground_anchor_q8"]} if asset["kind"] == "creatures" else {}),
                        "files": {k: {"bytes": len(v), "sha256": hashlib.sha256(v).hexdigest(), "runtime": k in runtime}
                                  for k, v in {**runtime, **derived}.items()}})
    pet = [r for r, asset in zip(records, manifest["assets"]) if asset["kind"] not in AGENT_ONLY]
    total = sum(r["bytes"] for r in pet)
    planned = total + DEFINITION_ALLOWANCE + METADATA_ALLOWANCE
    require(planned <= PACK_CEILING, f"Art exceeds the 6 MiB planned pack budget: {planned:,} bytes")
    report = {"pixel_bytes": total, "hires_assets": sum(1 for r in pet if r["density"] > 1),
              "agent_pixel_bytes": sum(r["bytes"] for r in records) - total,
              "definition_allowance": DEFINITION_ALLOWANCE, "metadata_allowance": METADATA_ALLOWANCE,
              "planned_pack_bytes": planned, "pack_ceiling": PACK_CEILING,
              "note": "Raw pixels are real runtime exports (RGB565 + mask, or RGB565 + 8-bit alpha for high-resolution art); derived masks of high-resolution art are inspection-only. Definitions and metadata remain allowances.", "assets": records}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def preview(output, manifest, images, report, source=SOURCE):
    for asset in manifest["assets"]:
        raw = (source / asset["path"]).read_bytes()
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
        # Legacy art is enlarged by nearest neighbour; high-resolution art is box-filtered
        # to the same logical sheet size, as the renderer does for downscales.
        resample = Image.Resampling.BOX if is_hires(asset) else Image.Resampling.NEAREST
        sprite = image.resize((asset["width"] * scale, asset["height"] * scale), resample)
        if asset["kind"] == "font":
            draw.rectangle((x + 8, y + 4, x + 151, y + 87), fill="#49334f")
        sheet.paste(sprite, (x + (160 - sprite.width) // 2, y + 8), sprite)
        draw.text((x + 5, y + 110), asset["key"].split(".", 1)[1], fill="#291b35")
        stored = f' @{asset["density"]}x' if is_hires(asset) else ""
        draw.text((x + 5, y + 126), f'{asset["width"]} x {asset["height"]}{stored} / {asset["id"]}', fill="#72516b")
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
    print(f'Validated {len(images)} assets ({report["hires_assets"]} high-resolution); '
          f'{report["pixel_bytes"]:,} raw bytes of {report["pack_ceiling"]:,}. Preview: {output / "preview.html"}')


if __name__ == "__main__":
    main()
