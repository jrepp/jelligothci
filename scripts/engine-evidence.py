#!/usr/bin/env -S ./scripts/uv run --script
# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Render deterministic SDL engine evidence from every authored scene."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SIZE = (466, 466)
WALL_MS = 1791331200000
EARLY_FRAMES = 1
LATE_FRAMES = 240
TRAVEL_SPEED = 8


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def scenes_from(path: Path) -> list[dict]:
    catalog = json.loads(path.read_text(encoding="utf-8"))
    scenes = catalog.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        raise ValueError(f"{path} has no scene catalog")
    keys = [scene.get("key") for scene in scenes]
    if any(not isinstance(key, str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", key) for key in keys) or len(keys) != len(set(keys)):
        raise ValueError(f"{path} has missing or duplicate scene keys")
    return scenes


def render(binary: Path, output: Path, scene: dict, variant: str, frames: int,
           travel_speed: int) -> dict:
    key = scene["key"]
    stem = f"{key}-{variant}"
    bmp = output / f"{stem}.bmp"
    png = output / f"{stem}.png"
    with tempfile.TemporaryDirectory(prefix=f"{stem}-save-", dir=output) as temporary_save:
        save = Path(temporary_save) / "pet"
        command = [str(binary), "--pet", "--headless", "--scene", key,
                   "--frames", str(frames), "--wall-ms", str(WALL_MS),
                   "--save", str(save), "--snapshot", str(bmp)]
        if travel_speed:
            command += ["--travel-speed", str(travel_speed)]
        started = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=30)
        elapsed = time.monotonic() - started
        if result.returncode:
            raise RuntimeError(f"render failed ({result.returncode}): {' '.join(command)}\n{result.stdout}\n{result.stderr}")
    if not bmp.is_file() or bmp.stat().st_size <= 54:
        raise RuntimeError(f"render produced no bitmap: {' '.join(command)}")
    with Image.open(bmp) as source:
        image = source.convert("RGB")
    if image.size != SIZE:
        raise RuntimeError(f"{key}/{variant} is {image.size}, expected {SIZE}")
    if len(image.getcolors(maxcolors=SIZE[0] * SIZE[1]) or []) < 8:
        raise RuntimeError(f"{key}/{variant} looks blank or nearly uniform")
    image.save(png, optimize=True)
    bmp.unlink()
    return {
        "scene": key, "variant": variant, "frames": frames,
        "travel_speed_px_s": travel_speed, "wall_ms": WALL_MS,
        "command": command, "elapsed_seconds": round(elapsed, 3),
        "png": png.name, "png_sha256": sha256(png), "dimensions": list(image.size),
        "colors": len(image.getcolors(maxcolors=SIZE[0] * SIZE[1]) or []),
    }


def make_sheets(records: list[dict], output: Path) -> list[dict]:
    font = ImageFont.load_default()
    sheets = []
    # Keep each contact sheet small enough to inspect in a browser.
    keys = list(dict.fromkeys(record["scene"] for record in records))
    for page, start in enumerate(range(0, len(keys), 4), 1):
        selected = keys[start:start + 4]
        width, height = 2 * SIZE[0], len(selected) * (SIZE[1] + 44)
        sheet = Image.new("RGB", (width, height), "#20242b")
        draw = ImageDraw.Draw(sheet)
        for row, key in enumerate(selected):
            for col, variant in enumerate(("early", "travel")):
                record = next(r for r in records if r["scene"] == key and r["variant"] == variant)
                with Image.open(output / record["png"]) as image:
                    x, y = col * SIZE[0], row * (SIZE[1] + 44)
                    sheet.paste(image.convert("RGB"), (x, y + 24))
                    label = f"{key} | {variant} | frame {record['frames']}"
                    draw.text((x + 8, y + 6), label, fill="white", font=font)
        path = output / f"contact-sheet-{page:02d}.png"
        sheet.save(path, optimize=True)
        sheets.append({"path": path.name, "sha256": sha256(path), "dimensions": list(sheet.size), "scenes": selected})
    return sheets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    if not binary.is_file() or not binary.stat().st_mode & 0o111:
        parser.error(f"not an executable file: {binary}")
    output.mkdir(parents=True, exist_ok=True)
    scenes = scenes_from(ROOT / "content/scenes.json")
    records = []
    for scene in scenes:
        records.append(render(binary, output, scene, "early", EARLY_FRAMES, 0))
        records.append(render(binary, output, scene, "travel", LATE_FRAMES, TRAVEL_SPEED))
    # Exercise actual compositor motion: the authored market layers have nonzero
    # travel and must produce a visibly different later/travel frame.
    early = Image.open(output / "market-street-early.png").convert("RGB")
    later = Image.open(output / "market-street-travel.png").convert("RGB")
    # This narrow upper-background strip avoids the title, pet sprite, and status
    # panel so the change check measures authored layer travel rather than animation.
    background_box = (0, 90, SIZE[0], 145)
    changed_pixels = sum(ImageChops.difference(early.crop(background_box), later.crop(background_box))
                         .convert("L").histogram()[1:])
    if changed_pixels < 100:
        raise RuntimeError("market-street travel render did not change from the early frame")
    sheets = make_sheets(records, output)
    manifest = {
        "schema_version": 1,
        "git_sha": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "binary": str(binary.relative_to(ROOT)) if binary.is_relative_to(ROOT) else binary.name,
        "binary_sha256": sha256(binary),
        "evidence_script_sha256": sha256(Path(__file__).resolve()),
        "working_tree_status": subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT, text=True
        ).splitlines(),
        "working_tree_clean": not subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT, text=True
        ).strip(),
        "scene_catalog": "content/scenes.json",
        "scene_catalog_sha256": sha256(ROOT / "content/scenes.json"),
        "render_contract": {"dimensions": list(SIZE), "wall_ms": WALL_MS,
                            "early_frames": EARLY_FRAMES, "travel_frames": LATE_FRAMES,
                            "travel_speed_px_s": TRAVEL_SPEED},
        "scene_count": len(scenes), "renders": records, "contact_sheets": sheets,
        "checks": {"all_commands_succeeded": True, "all_images_nonblank": True,
                   "expected_dimensions": list(SIZE), "market_travel_changed_pixels": changed_pixels},
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Rendered {len(records)} images across {len(scenes)} scenes; manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"engine evidence: {error}", file=sys.stderr)
        raise SystemExit(1)
