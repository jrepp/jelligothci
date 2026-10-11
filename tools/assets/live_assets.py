# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Watch authored PNGs and atomically publish a bounded SDL artwork pack."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from PIL import Image
from build_slice import pack_mask, PACK_CEILING, LIVE_PIXEL_CAPACITY, LIVE_MASK_CAPACITY
from sprite_geometry import ground_anchor_q8, opaque_centroid_q8

ROOT = Path(__file__).resolve().parents[2]
MAX_PACK_BYTES = PACK_CEILING
MAX_ASSETS = 192
HEADER = struct.Struct("<4sIII")
RECORD = struct.Struct("<IHHHHHHBBBBI")
# Mirrors tools/assets/build_slice.py: creatures may be 32x32 or 48x48.
DIMENSIONS = {
    "creatures": {(32, 32), (48, 48)}, "icons": {(16, 16)}, "props": {(16, 16), (24, 24), (32, 32)},
    "font": {(128, 72)}, "menus": {(32, 32)}, "meters": {(32, 32)},
    "health": {(32, 32)}, "effects": {(16, 16)}, "backgrounds": {(64, 64)},
    "prizes": {(32, 32)},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def asset_image(source, asset):
    path = (source / asset["path"]).resolve()
    require(source in path.parents, f"Asset path escapes source: {asset['path']}")
    dimensions = DIMENSIONS.get(asset["kind"])
    require(dimensions is not None, f"Unknown asset kind: {asset['kind']}")
    require((asset["width"], asset["height"]) in dimensions,
            f"Manifest dimensions changed: {asset['path']}")
    with Image.open(path) as opened:
        require(opened.format == "PNG", f"Expected PNG: {asset['path']}")
        require(opened.size == (asset["width"], asset["height"]), f"Wrong dimensions: {asset['path']}")
        image = opened.convert("RGBA")
    alpha = image.getchannel("A")
    require(set(alpha.getdata()) <= {0, 255}, f"Nonbinary alpha: {asset['path']}")
    require(alpha.getbbox() is not None, f"Empty artwork: {asset['path']}")
    return image


def payload(image, font):
    if font:
        glyphs = bytearray()
        for index in range(96):
            x, y = index % 16 * 8, index // 16 * 12
            glyphs.extend(pack_mask(image.crop((x, y, x + 8, y + 12))))
        return bytes(glyphs)
    pixels = bytearray()
    for r, g, b, _ in image.getdata():
        pixels.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    return bytes(pixels) + pack_mask(image)


def build_pack(source):
    source = Path(source).resolve()
    manifest = json.loads((source / "assets.json").read_text())
    require(isinstance(manifest, dict), "Manifest must be an object")
    require(manifest.get("schema_version") == 1, "Unknown manifest schema")
    assets = manifest["assets"]
    require(0 < len(assets) <= MAX_ASSETS, "Asset count exceeds pack bounds")
    ids, paths = set(), set()
    body = bytearray()
    pixels, masks = 0, 0
    for asset in sorted(assets, key=lambda item: item["id"]):
        ident = asset["id"]
        require(type(ident) is int and 0 < ident < 2**32, "Invalid asset ID")
        require(ident not in ids and asset["path"] not in paths, "Duplicate asset ID/path")
        ids.add(ident)
        paths.add(asset["path"])
        image = asset_image(source, asset)
        font = asset["kind"] == "font"
        require(not font or ident == 4001, "Font must retain ID 4001")
        if not font:
            pixels += image.width * image.height
            masks += (image.width + 7) // 8 * image.height
            require(pixels <= LIVE_PIXEL_CAPACITY and masks <= LIVE_MASK_CAPACITY,
                    "Artwork exceeds live banks")
        data = payload(image, font)
        bounds = image.getchannel("A").getbbox()
        ground = ground_anchor_q8(image) if asset["kind"] == "creatures" else (0, 0)
        center = opaque_centroid_q8(image)
        body.extend(RECORD.pack(ident, image.width, image.height, *ground, *center,
                                *bounds, len(data)))
        body.extend(data)
        require(HEADER.size + len(body) <= MAX_PACK_BYTES,
                f"Asset pack exceeds {MAX_PACK_BYTES}-byte buffer")
    return HEADER.pack(b"JLAP", 1, len(assets), zlib.crc32(body)) + body


def publish_pack(source, destination):
    """Validate everything before replacing the previous successful pack."""
    source = Path(source).resolve()
    before = fingerprint(source)
    data = build_pack(source)
    require(before == fingerprint(source), "Assets changed while packing; waiting for stable files")
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=destination.name + ".",
                                         suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return len(data)


def fingerprint(source):
    files = [source / "assets.json", *sorted(source.rglob("*.png"))]
    result = []
    for path in files:
        try:
            stat = path.stat()
            result.append((str(path.relative_to(source)), stat.st_mtime_ns, stat.st_size))
        except FileNotFoundError:
            result.append((str(path.relative_to(source)), None, None))
    return tuple(result)


class Publisher:
    def __init__(self, source, destination, report=print):
        self.source, self.destination, self.report = source, destination, report
        self.last_error = None

    def refresh(self):
        try:
            size = publish_pack(self.source, self.destination)
        except (OSError, ValueError, KeyError, TypeError, struct.error,
                Image.DecompressionBombError) as error:
            message = f"Artwork unchanged: {error}"
            if message != self.last_error:
                self.report(message)
            self.last_error = message
            return False
        self.last_error = None
        self.report(f"Artwork published: {size} bytes → {self.destination}")
        return True


def watch(child, publisher, debounce):
    observed = fingerprint(publisher.source)
    pending = time.monotonic() if publisher.last_error else None
    while child.poll() is None:
        current = fingerprint(publisher.source)
        if current != observed:
            observed = current
            pending = time.monotonic()
        if pending is not None and time.monotonic() - pending >= debounce:
            publisher.refresh()
            pending = None
        time.sleep(0.1)
    return child.returncode


def find_binary(root):
    candidates = [root / "build/desktop/jelligotchi",
                  root / "Jelligotchi.app/Contents/MacOS/jelligotchi", root / "jelligotchi.exe"]
    return next((path for path in candidates if path.is_file()), candidates[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", "--source", dest="source", type=Path,
                        default=ROOT / "assets/slice")
    parser.add_argument("--pack", type=Path, default=ROOT / "build/assets/live.jlap")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--debounce", type=float, default=0.5)
    parser.add_argument("--build-only", action="store_true")
    args, game_args = parser.parse_known_args()
    if game_args[:1] == ["--"]:
        game_args = game_args[1:]
    if not 0.1 <= args.debounce <= 60:
        parser.error("--debounce must be between 0.1 and 60 seconds")
    publisher = Publisher(args.source.resolve(), args.pack.resolve(),
                          report=lambda message: print(message, flush=True))
    valid = publisher.refresh()
    if args.build_only:
        return 0 if valid else 1
    if not valid and not publisher.destination.is_file():
        return 1
    try:
        binary = args.binary.resolve() if args.binary else find_binary(ROOT)
        child = subprocess.Popen([str(binary), "--pet", "--asset-pack",
                                  str(publisher.destination), *game_args], cwd=ROOT)
    except OSError as error:
        print(f"Cannot launch SDL: {error}", file=sys.stderr)
        return 1
    try:
        return watch(child, publisher, args.debounce)
    except KeyboardInterrupt:
        return 130
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


if __name__ == "__main__":
    raise SystemExit(main())
