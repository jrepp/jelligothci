# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""The SDL pack decoder accepts live packs of legacy, high-resolution and shipped art.

Decoding re-measures every asset in C, so acceptance proves the C and Python
coverage, bounds, centroid and contact formulas agree at every density, including
on the real soft-edged artwork in assets/slice.
"""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/assets"))
sys.path.insert(0, str(ROOT / "tests"))
import live_assets  # noqa: E402
from hires_fixture import make_source  # noqa: E402
from PIL import Image  # noqa: E402

binary = sys.argv[1]


def decode(pack):
    return subprocess.run([binary, str(pack)], capture_output=True, text=True, timeout=120)


with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    hires, _ = make_source(folder / "hires")
    legacy, _ = make_source(folder / "legacy", legacy=True)
    for name, source in (("legacy", legacy), ("hires", hires), ("shipped", ROOT / "assets/slice")):
        data = live_assets.build_pack(source)
        pack = folder / f"{name}.jlap"
        pack.write_bytes(data)
        result = decode(pack)
        assert result.returncode == 0, (name, result.stderr)
        decoded = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
        manifest = json.loads((source / "assets.json").read_text())
        # The pet's live pack leaves out Loki Agent's own art.
        expected = sorted((a["id"], a.get("density", 1), int("density" in a)) for a in manifest["assets"]
                          if a["kind"] not in live_assets.AGENT_ONLY)
        assert decoded == expected, name
        # Stale metadata: nudge the first record's (1001) centroid by 1/256 pixel.
        tampered = bytearray(data)
        offset = live_assets.HEADER.size + 12
        tampered[offset] ^= 1
        pack.write_bytes(bytes(tampered))
        assert decode(pack).returncode != 0, name

# Geometry agreement beyond the shipped frames: seeded edits to real art (alpha noise
# straddling the 128 coverage threshold, cleared and filled rectangles, toggled legacy
# mask pixels) are measured by the Python tooling and must be accepted by the C
# decoder, which re-measures them; nudging any one metadata field by one is refused.
SLICE = ROOT / "assets/slice"
FIELDS = {"ground_x": (8, "<H"), "ground_y": (10, "<H"), "centroid_x": (12, "<H"),
          "centroid_y": (14, "<H"), "left": (16, "<B"), "top": (17, "<B"), "right": (18, "<B"),
          "bottom": (19, "<B")}


def mutate_image(image, rng, hires):
    pixels = image.load()
    width, height = image.size
    for _ in range(rng.randint(40, 400)):
        x, y = rng.randrange(width), rng.randrange(height)
        r, g, b, a = pixels[x, y]
        if hires:
            a = rng.choice([0, 1, 126, 127, 128, 129, 254, 255, rng.randrange(256)])
        else:
            a = 255 - a
        pixels[x, y] = (r, g, b, a)
    if hires and rng.random() < 0.5:  # A cleared or filled rectangle.
        x0, y0 = rng.randrange(width), rng.randrange(height)
        x1, y1 = min(width, x0 + rng.randint(1, width // 3)), min(height, y0 + rng.randint(1, height // 3))
        fill = rng.choice([0, 255])
        for y in range(y0, y1):
            for x in range(x0, x1):
                r, g, b, _ = pixels[x, y]
                pixels[x, y] = (r, g, b, fill)
    if not any(a >= 128 for a in image.getchannel("A").getdata()):
        pixels[width // 2, height // 2] = (255, 255, 255, 255)  # Never empty.
    return image


def mutated_pack(folder, seed):
    rng = random.Random(seed)
    manifest = json.loads((SLICE / "assets.json").read_text())
    # The pet's pack: Loki Agent's own art never goes in it.
    pet = [a for a in manifest["assets"] if a["kind"] not in live_assets.AGENT_ONLY]
    hires = [a for a in pet if "density" in a]
    legacy = [a for a in pet if "density" not in a and a["kind"] != "font"]
    chosen = rng.sample(hires, 6) + rng.sample(legacy, 1)
    source = folder / f"mutated-{seed}"
    for asset in chosen:
        with Image.open(SLICE / asset["path"]) as opened:
            image = mutate_image(opened.convert("RGBA"), rng, "density" in asset)
        path = source / asset["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        image.save(path)
    (source / "assets.json").write_text(json.dumps({**manifest, "assets": chosen}))
    return live_assets.build_pack(source), chosen


def records(data):
    at = live_assets.HEADER.size
    while at < len(data):
        fields = live_assets.RECORD.unpack_from(data, at)
        yield at, fields
        at += live_assets.RECORD.size + fields[11]


with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    tampered_count = 0
    for seed in range(8):
        data, chosen = mutated_pack(folder, seed)
        pack = folder / "mutated.jlap"
        pack.write_bytes(data)
        result = decode(pack)
        assert result.returncode == 0, (seed, result.stderr)
        decoded = sorted(tuple(map(int, line.split())) for line in result.stdout.splitlines())
        assert decoded == sorted((a["id"], a.get("density", 1), int("density" in a)) for a in chosen)
        rng = random.Random(1000 + seed)
        for at, fields in rng.sample(list(records(data)), 3):
            name = rng.choice(sorted(FIELDS))
            offset, layout = FIELDS[name]
            value = struct.unpack_from(layout, data, at + offset)[0]
            limit = 0xFFFF if layout == "<H" else 0xFF
            tampered = bytearray(data)
            struct.pack_into(layout, tampered, at + offset, value + 1 if value < limit else value - 1)
            pack.write_bytes(bytes(tampered))
            assert decode(pack).returncode != 0, (seed, fields[0], name)
            tampered_count += 1
    print(f"mutated art: 8 packs of 7 edited assets accepted; {tampered_count} one-field nudges refused")
print("PASS: C decoder accepts legacy, high-resolution and shipped live packs; stale metadata rejected")
