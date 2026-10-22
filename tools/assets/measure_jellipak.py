#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0", "lz4==4.4.5"]
# ///
"""Jellipak design experiment: independently decodable indexed asset blocks.

Run: ./scripts/uv run --script tools/assets/measure_jellipak.py
No generated firmware or loader is changed. JSON on stdout is deterministic at
one source revision and records every asset plus aggregate payload byte counts.
"""
import hashlib
import json
import platform
import struct
import subprocess
from collections import Counter, defaultdict
from importlib.metadata import version
from pathlib import Path

import lz4
import lz4.block
from build_slice import REPO, SOURCE, load_assets, pack_mask, palette_colours
from embed_slice import rgb565


def rle_encode(data):
    """PackBits-like: 0..127 => 1..128 literals; 128..255 => 1..128 repeats."""
    out = bytearray()
    at = 0
    while at < len(data):
        run = 1
        while at + run < len(data) and run < 128 and data[at + run] == data[at]:
            run += 1
        if run >= 3:
            out.extend((127 + run, data[at]))
            at += run
            continue
        start = at
        at += run
        while at < len(data) and at - start < 128:
            if at + 2 < len(data) and data[at] == data[at + 1] == data[at + 2]:
                break
            at += 1
        out.append(at - start - 1)
        out.extend(data[start:at])
    return bytes(out)


def rle_decode(data):
    out = bytearray()
    at = 0
    while at < len(data):
        control = data[at]
        at += 1
        count = (control & 127) + 1
        if control & 128:
            out.extend(bytes([data[at]]) * count)
            at += 1
        else:
            out.extend(data[at:at + count])
            at += count
    return bytes(out)


def compress(data):
    return lz4.block.compress(data, mode="high_compression", compression=12, store_size=False)


def grouped_measurements(manifest, images, raw_blocks, compact_palette_bytes):
    """Compare locality and authored-palette sharing with bounded LZ4 chunks."""
    owners = defaultdict(set)
    traversal = defaultdict(list)
    for clip in manifest["clips"]:
        form = clip["key"].split(".")[0]
        for key in clip["frames"]:
            owners[key].add(form)
            if key not in traversal[form]:
                traversal[form].append(key)
    assets = {a["key"]: a for a in manifest["assets"]}
    groups = defaultdict(list)
    for form, keys in traversal.items():
        for key in keys:
            if assets[key]["kind"] == "creatures" and len(owners[key]) == 1:
                groups["form:" + form].append(key)
    assigned = {key for keys in groups.values() for key in keys}
    for asset in sorted(manifest["assets"], key=lambda a: a["id"]):
        key, kind = asset["key"], asset["kind"]
        if key in assigned:
            continue
        group = ("asset:" + key) if kind == "creatures" else ("kind:" + kind)
        groups[group].append(key)
    assert sorted(key for keys in groups.values() for key in keys) == sorted(raw_blocks)
    # Palette sharing respects creature-form locality, while background batches
    # can span scenes and compatible UI assets can span kinds. Authored palettes
    # are canonicalized by RGB565 value, not by their human-readable names.
    shared_groups = defaultdict(list)
    for name, keys in groups.items():
        for key in keys:
            asset = assets[key]
            if asset["kind"] == "font":
                shared_groups["font"].append(key)
                continue
            authored = sorted({rgb565(*bytes.fromhex(c[1:])) for c in palette_colours(manifest, asset)})
            scope = name if name.startswith(("form:", "asset:")) else (
                "backgrounds" if asset["kind"] == "backgrounds" else "ui")
            fingerprint = hashlib.sha256(struct.pack("<" + "H" * len(authored), *authored)).hexdigest()[:16]
            shared_groups[scope + ":palette:" + fingerprint].append(key)
    shared_raw, shared_palettes, group_palette_sizes = {}, set(), {}
    for name, keys in shared_groups.items():
        if name == "font":
            shared_raw.update((key, raw_blocks[key]) for key in keys)
            continue
        colours = sorted({rgb565(r, g, b) for key in keys for r, g, b, a in images[key].getdata() if a})
        assert 0 < len(colours) <= 16
        shared_palettes.add(struct.pack("<" + "H" * len(colours), *colours))
        group_palette_sizes[name] = 2 * len(colours)
        for key in keys:
            image = images[key]
            rgba = list(image.getdata())
            indices = [colours.index(rgb565(r, g, b)) if a else 0 for r, g, b, a in rgba]
            packed = bytes((indices[y * image.width + x] << 4)
                           | (indices[y * image.width + x + 1] if x + 1 < image.width else 0)
                           for y in range(image.height) for x in range(0, image.width, 2))
            stride = (image.width + 1) // 2
            assert all(colours[(packed[y * stride + x // 2] >> (0 if x % 2 else 4)) & 15]
                       == rgb565(*rgba[y * image.width + x][:3])
                       for y in range(image.height) for x in range(image.width)
                       if rgba[y * image.width + x][3])
            shared_raw[key] = packed + pack_mask(image)
            assert len(shared_raw[key]) == len(raw_blocks[key])
    assert sorted(shared_raw) == sorted(raw_blocks)
    results = []
    variants = [("compact_per_asset", groups, raw_blocks, compact_palette_bytes),
                ("shared_compatible", shared_groups, shared_raw, sum(map(len, shared_palettes)))]
    for representation, candidate_groups, candidate_raw, palette_bytes in variants:
        for order in ("clip_traversal", "asset_id"):
            for limit in (8192, 16384, 32768):
                records = []
                for name, members in sorted(candidate_groups.items()):
                    keys = members if order == "clip_traversal" else sorted(members, key=lambda k: assets[k]["id"])
                    chunks, chunk, size = [], [], 0
                    for key in keys:
                        count = len(candidate_raw[key])
                        assert count <= limit
                        if chunk and size + count > limit:
                            chunks.append(chunk)
                            chunk, size = [], 0
                        chunk.append(key)
                        size += count
                    if chunk:
                        chunks.append(chunk)
                    blocks = []
                    for chunk in chunks:
                        raw = b"".join(candidate_raw[key] for key in chunk)
                        encoded = compress(raw)
                        assert lz4.block.decompress(encoded, uncompressed_size=len(raw)) == raw
                        offsets, offset = [], 0
                        for key in chunk:
                            length = len(candidate_raw[key])
                            offsets.append({"key": key, "offset": offset, "length": length})
                            offset += length
                        assert all(raw[item["offset"]:item["offset"] + item["length"]]
                                   == candidate_raw[item["key"]] for item in offsets)
                        blocks.append({"assets": offsets, "raw": len(raw), "lz4": len(encoded),
                                       "selected": min(len(raw), len(encoded)),
                                       "max_extra_decoded_on_miss": max(len(raw) - len(candidate_raw[k]) for k in chunk)})
                    records.append({"group": name, "blocks": blocks,
                                    "encoded_bytes": sum(b["selected"] for b in blocks)})
                blocks = [b for group in records for b in group["blocks"]]
                results.append({"representation": representation, "palette_bytes": palette_bytes,
                                "palette_bytes_without_group_dedup": (sum(group_palette_sizes.values())
                                if representation == "shared_compatible" else None),
                            "order": order, "decoded_limit": limit, "block_count": len(blocks),
                                "encoded_bytes": sum(b["selected"] for b in blocks),
                                "largest_decode_chunk": max(b["raw"] for b in blocks),
                                "max_extra_decoded_on_miss": max(b["max_extra_decoded_on_miss"] for b in blocks),
                                "groups": records})
    return results


def measure():
    manifest, images = load_assets()
    rows = []
    raw_blocks = {}
    palettes = set()
    source_hash = hashlib.sha256()
    for path in sorted({SOURCE / "assets.json"} | {SOURCE / a["path"] for a in manifest["assets"]}):
        source_hash.update(path.relative_to(REPO).as_posix().encode() + b"\0")
        source_hash.update(path.read_bytes())
    for asset in manifest["assets"]:
        image = images[asset["key"]]
        rgba = list(image.getdata())
        mask = pack_mask(image)
        if asset["kind"] == "font":
            # Match embed_slice's glyph-major layout, rather than sheet rows.
            raw = b"".join(pack_mask(image.crop(((i % 16) * 8, (i // 16) * 12,
                                                (i % 16 + 1) * 8, (i // 16 + 1) * 12)))
                           for i in range(96))
            baseline = len(raw)
            palette = b""
        else:
            colours = sorted({rgb565(r, g, b) for r, g, b, a in rgba if a})
            assert 0 < len(colours) <= 16
            palette = struct.pack("<" + "H" * len(colours), *colours)
            palettes.add(palette)
            indices = [colours.index(rgb565(r, g, b)) if a else 0 for r, g, b, a in rgba]
            packed = bytes((indices[y * image.width + x] << 4)
                           | (indices[y * image.width + x + 1] if x + 1 < image.width else 0)
                           for y in range(image.height) for x in range(0, image.width, 2))
            # Invisible RGB is deliberately normalized to index zero; alpha is
            # separate, so all sixteen colour indices remain available.
            index_stride = (image.width + 1) // 2
            decoded = [colours[(packed[y * index_stride + x // 2]
                                >> (0 if x % 2 else 4)) & 15]
                       for y in range(image.height) for x in range(image.width)]
            assert all(decoded[i] == rgb565(r, g, b) for i, (r, g, b, a) in enumerate(rgba) if a)
            decoded_mask = mask
            assert all(bool(decoded_mask[(i // image.width) * ((image.width + 7) // 8) + (i % image.width) // 8]
                            & (1 << (7 - (i % image.width) % 8))) == bool(pixel[3])
                       for i, pixel in enumerate(rgba))
            raw = packed + mask
            baseline = len(rgba) * 2 + len(mask)
        raw_blocks[asset["key"]] = raw
        rle = rle_encode(raw)
        compressed = compress(raw)
        combined = compress(rle)
        assert rle_decode(rle) == raw
        assert lz4.block.decompress(compressed, uncompressed_size=len(raw)) == raw
        assert rle_decode(lz4.block.decompress(combined, uncompressed_size=len(rle))) == raw
        candidates = {"raw": raw, "rle": rle, "lz4": compressed, "rle_lz4": combined}
        selected = min(candidates, key=lambda codec: len(candidates[codec]))
        rows.append({"key": asset["key"], "kind": asset["kind"], "baseline": baseline,
                     "palette_bytes": len(palette), "sizes": {c: len(b) for c, b in candidates.items()},
                     "selected": selected, "selected_bytes": len(candidates[selected]),
                     "raw_sha256": hashlib.sha256(raw).hexdigest()})
    totals = defaultdict(Counter)
    for row in rows:
        counts = {"assets": 1, "baseline": row["baseline"], "palette_per_asset": row["palette_bytes"],
                  "selected": row["selected_bytes"], **row["sizes"]}
        totals[row["kind"]].update(counts)
        totals["all"].update(counts)
    return {"source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
            "source_assets_sha256": source_hash.hexdigest(),
            "source_assets_git_status": subprocess.check_output(
                ["git", "status", "--porcelain", "--", "assets/slice/assets.json",
                 *["assets/slice/" + a["path"] for a in manifest["assets"]]],
                cwd=REPO, text=True).splitlines(),
            "measurement_script_sha256": hashlib.sha256(
                Path(__file__).read_bytes()).hexdigest(),
            "versions": {"python": platform.python_version(), "Pillow": version("Pillow"), "lz4": version("lz4"), "lz4_library": lz4.library_version_string()},
            "codec": "LZ4 raw independent blocks; HC12; no size prefix; byte RLE literals/repeats up to 128",
            "palette_unique_bytes": sum(map(len, palettes)), "palette_unique_count": len(palettes),
            "selected_codecs": dict(Counter(r["selected"] for r in rows)),
            "totals": dict(totals), "assets": rows,
            "grouped": grouped_measurements(manifest, images, raw_blocks, sum(map(len, palettes)))}


if __name__ == "__main__":
    # Exercise literal boundaries and repeat limits as well as real source art.
    for sample in [b"", b"a", b"a" * 129, bytes(range(256)), bytes(range(128)) + b"aa"]:
        assert rle_decode(rle_encode(sample)) == sample
    report = measure()
    assert report == measure(), "Nondeterministic result"
    print(json.dumps(report, indent=2, sort_keys=True))
