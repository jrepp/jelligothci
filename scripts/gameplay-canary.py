#!/usr/bin/env -S ./scripts/uv run --script
# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Capture a nine-scenario gameplay grid through the native pet engine."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/jelli-art"))
from behaviors import c_enums  # noqa: E402

SIZE = (466, 466)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: str) -> dict:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def named(items: list[dict], field: str, value: str) -> dict:
    matches = [item for item in items if item.get(field) == value]
    if len(matches) != 1:
        raise ValueError(f"expected one {field}={value!r}, got {len(matches)}")
    return matches[0]


def scenarios() -> list[dict]:
    pets = load("content/pets.json")
    activities = load("content/activities.json")
    scenes = load("content/scenes.json")
    effects = load("content/effects.json")
    prizes = load("content/prizes.json")
    pages = c_enums([ROOT / "include/jelli/pet_ui.h"])
    jelli = named(pets["entries"], "name", "JELLI")["id"]
    bubble = named(pets["entries"], "name", "BUBBLE")["id"]
    forms = {item["name"]: item["id"] for item in pets["forms"]}
    for name in ("MINT", "LILAC", "BABY AXO", "AXOLOTL"):
        if name not in forms:
            raise ValueError(f"missing form {name}")
    for key in ("market-street", "garden-walk"):
        named(scenes["scenes"], "key", key)
    named(effects["effects"], "key", "celebrate")
    if not prizes.get("prizes"):
        raise ValueError("missing collectible prizes")
    breakfast = named(activities["moments"], "name", "BREAKFAST")
    # Authored windows may change. Use a minute inside the available window.
    minute = (breakfast["start_minute"] + breakfast["end_minute"]) // 2
    breakfast_id = breakfast["id"] + 1  # Preview and runtime store moment IDs one-based.
    home, care, collection = (pages["JELLI_UI_" + name] for name in ("HOME", "CARE", "COLLECTION"))
    def case(key, label, entry, form, args, expected):
        return {"key": key, "label": label, "args": ["--entry", str(entry), "--form", str(forms[form]), *args],
                "expected": {"ok": True, "entry": entry, "form": forms[form], "form_name": form, **expected}}
    return [
        case("mint-home", "Home · Mint", jelli, "MINT", [], {"page": home, "menu": False, "moment": 0, "activity": "idle"}),
        case("lilac-market", "Market · Lilac", jelli, "LILAC", ["--scene", "market-street"], {"page": home, "menu": False}),
        case("baby-axo", "Garden · Baby Axo", bubble, "BABY AXO", ["--scene", "garden-walk"], {"page": home, "menu": False}),
        case("main-menu", "Main menu · Axolotl", bubble, "AXOLOTL", ["--menu", "1"], {"page": home, "menu": True}),
        case("care-menu", "Care menu · Mint", jelli, "MINT", ["--page", str(care), "--menu", "1"], {"page": care, "menu": True}),
        case("breakfast", "Breakfast · Eating", jelli, "MINT", ["--minute", str(minute), "--moment", str(breakfast_id)], {"page": home, "menu": False, "moment": breakfast_id, "activity": "eating"}),
        case("collection", "Collection · One gift", jelli, "MINT", ["--page", str(collection), "--menu", "1", "--present", "1", "--present_state", "2"], {"page": collection, "menu": True, "owned": 1}),
        case("celebration", "Celebration · Mint", jelli, "MINT", ["--effect", "celebrate", "--start_ms", "750"], {"page": home, "menu": False}),
        case("garden", "Garden walk · Mint", jelli, "MINT", ["--scene", "garden-walk"], {"page": home, "menu": False}),
    ]


def validate_state(key: str, state: dict, expected: dict) -> None:
    for field, wanted in expected.items():
        if state.get(field) != wanted:
            raise ValueError(f"{key}: {field} is {state.get(field)!r}, expected {wanted!r}")


def render(binary: Path, folder: Path, scenario: dict) -> dict:
    key = scenario["key"]
    frame_dir = folder / key
    frame_dir.mkdir(parents=True, exist_ok=True)
    args = scenario["args"]
    if "--minute" not in args:
        args = ["--minute", "720", *args]
    command = [str(binary), "--out", str(frame_dir), *args]
    started = time.monotonic()
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f"{key}: native render failed ({result.returncode}): {result.stdout} {result.stderr}")
    state = json.loads(result.stdout)
    validate_state(key, state, scenario["expected"])
    source = frame_dir / "frame-000.ppm"
    if not source.is_file():
        raise RuntimeError(f"{key}: missing native frame")
    with Image.open(source) as frame:
        image = frame.convert("RGB")
    if image.size != SIZE or len(image.getcolors(maxcolors=SIZE[0] * SIZE[1]) or []) < 3:
        raise ValueError(f"{key}: frame has invalid dimensions or is blank")
    png = frame_dir / "frame.png"
    image.save(png, optimize=True)
    state_path = frame_dir / "state.json"
    state_path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    return {"key": key, "label": scenario["label"], "command": command,
            "expected": scenario["expected"], "state": state,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "raw": str(source.relative_to(folder)), "raw_sha256": digest(source),
            "png": str(png.relative_to(folder)), "png_sha256": digest(png),
            "state_path": str(state_path.relative_to(folder)), "state_sha256": digest(state_path)}


def changed_pixels(a: Path, b: Path) -> int:
    with Image.open(a) as first, Image.open(b) as second:
        diff = ImageChops.difference(first.convert("RGB"), second.convert("RGB"))
        return sum(diff.convert("L").histogram()[1:])


def grid(records: list[dict], folder: Path, path: Path) -> None:
    if len(records) != 9:
        raise ValueError("the gameplay grid requires exactly nine scenarios")
    heading = ImageFont.load_default(size=22)
    small = ImageFont.load_default(size=17)
    label_height = 62
    enums = c_enums([ROOT / "include/jelli/pet_ui.h"])
    page_names = {enums[name]: name.removeprefix("JELLI_UI_").lower()
                  for name in ("JELLI_UI_HOME", "JELLI_UI_CARE", "JELLI_UI_COLLECTION")}
    sheet = Image.new("RGB", (3 * SIZE[0], 3 * (SIZE[1] + label_height)), "#20242b")
    draw = ImageDraw.Draw(sheet)
    for index, record in enumerate(records):
        x, y = (index % 3) * SIZE[0], (index // 3) * (SIZE[1] + label_height)
        state = record["state"]
        draw.text((x + 10, y + 5), f"{index + 1}. {record['label']}", font=heading, fill="white")
        draw.text((x + 10, y + 34), f"{state['form_name']} | {state['activity']} | {page_names[state['page']]} | menu {'open' if state['menu'] else 'closed'}", font=small, fill="#cbd5e1")
        with Image.open(folder / record["png"]) as image:
            sheet.paste(image.convert("RGB"), (x, y + label_height))
    sheet.save(path, optimize=True)
    if path.stat().st_size > 10 * 1024 * 1024:
        raise ValueError("gameplay grid exceeds 10 MiB")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    binary, output = options.binary.resolve(), options.output.resolve()
    if not binary.is_file() or not binary.stat().st_mode & 0o111:
        parser.error(f"not an executable file: {binary}")
    output.mkdir(parents=True, exist_ok=True)
    folder = output / "canary"
    folder.mkdir(exist_ok=True)
    cases = scenarios()
    records = [render(binary, folder, case) for case in cases]
    # Prove the effect actually changed pixels at the same game time and state.
    control_case = {**cases[0], "key": "celebration-control", "args": [*cases[0]["args"], "--start_ms", "750"]}
    control = render(binary, folder, control_case)
    effect = next(record for record in records if record["key"] == "celebration")
    home = records[0]
    garden = next(record for record in records if record["key"] == "garden")
    effect_pixels = changed_pixels(folder / effect["png"], folder / control["png"])
    scene_pixels = changed_pixels(folder / garden["png"], folder / home["png"])
    if effect_pixels < 100 or scene_pixels < 100:
        raise ValueError(f"missing visible effect or scene: effect={effect_pixels}, scene={scene_pixels}")
    canary = output / "canary.png"
    grid(records, folder, canary)
    sources = ["content/pets.json", "content/activities.json", "content/prizes.json", "content/scenes.json", "content/effects.json", "include/jelli/pet_ui.h"]
    working_tree = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT, text=True
    ).splitlines()
    manifest = {"schema_version": 1,
                "working_tree_status": working_tree, "working_tree_clean": not working_tree,
                "git_sha": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "binary": str(binary.relative_to(ROOT)) if binary.is_relative_to(ROOT) else binary.name,
                "binary_sha256": digest(binary), "script_sha256": digest(Path(__file__).resolve()),
                "sources_sha256": {source: digest(ROOT / source) for source in sources},
                "grid": {"path": "../canary.png", "sha256": digest(canary), "columns": 3, "rows": 3},
                "scenarios": records, "control": control,
                "checks": {"effect_changed_pixels": effect_pixels, "garden_changed_pixels": scene_pixels}}
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Captured {len(records)} validated engine scenarios: {canary}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"gameplay canary: {error}", file=sys.stderr)
        raise SystemExit(1)
