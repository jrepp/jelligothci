# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Render the polished artwork as review candidates, or apply them to the sources.

Default: copy assets/slice to build/assets/polish with polished PNGs and bounds,
ready for tools/assets/compare_slice.py. --apply writes only the PNGs this recipe
owns and their manifest bounds; every other manifest field is left untouched.
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from creature_art import creatures  # noqa: E402
from icon_polish import icons  # noqa: E402
from keepsake_art import keepsakes  # noqa: E402
from pixel_kit import to_image  # noqa: E402
from prop_art import props  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
SOURCE = REPO / "assets/slice"


def hand_painted(source):
    """Asset paths an artist has saved from Jelli Art; the recipe leaves them alone."""
    listing = source / "source/hand-painted.json"
    keys = set(json.loads(listing.read_text())) if listing.exists() else set()
    manifest = json.loads((source / "assets.json").read_text())
    return {a["path"] for a in manifest["assets"] if a["key"] in keys}


def render(source):
    sprites = {**icons(source), **creatures(), **props(), **keepsakes()}
    skip = hand_painted(source)
    images = {}
    for path, sprite in sprites.items():
        if path in skip:
            continue
        size = (24, 24) if path.startswith("props/") else (16, 16) if path.startswith(("icons/", "effects/")) else (32, 32)
        images[path] = to_image(sprite, size)
    return images


def write(root, images):
    manifest_path = root / "assets.json"
    manifest = json.loads(manifest_path.read_text())
    for asset in manifest["assets"]:
        image = images.get(asset["path"])
        if image is not None:
            image.save(root / asset["path"])
            asset["bounds"] = list(image.getchannel("A").getbbox())
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=REPO / "build/assets/polish")
    parser.add_argument("--apply", action="store_true", help="overwrite the owned source PNGs and bounds")
    args = parser.parse_args()
    images = render(SOURCE)
    if args.apply:
        write(SOURCE, images)
        print(f"Applied {len(images)} polished PNGs to {SOURCE}")
        return
    output = args.output.resolve()
    if output == SOURCE.resolve() or SOURCE.resolve() in output.parents:
        raise SystemExit("Use --apply to write into the source tree")
    shutil.rmtree(output, ignore_errors=True)
    shutil.copytree(SOURCE, output, ignore=shutil.ignore_patterns("source", "__pycache__"))
    write(output, images)
    print(f"Wrote {len(images)} candidates to {output}")


if __name__ == "__main__":
    main()
