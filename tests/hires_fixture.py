# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Generate candidate asset directories from the retained legacy pixel art.

Test and measurement support only; nothing here is shipped art. The shipped
manifest points at high-resolution PNGs under `hires/`; the original 32x32 (etc.)
pixel art still sits beside them under `<kind>/<name>.png`. This module rebuilds
that legacy view (legacy paths, bounds measured from the legacy PNGs, no density;
the unicorn, which has no legacy art, uses Lilac's frames as stand-ins) and can
write it either as is (`legacy=True`) or upscaled to production densities.

Each upscaled PNG is nearest-upscaled with a soft edge: covered boundary pixels get
alpha 192, the uncovered ring around them alpha 64 (ink colour), and low colour
bits vary so the art is off-palette. Coverage (alpha >= 128) is exactly the
upscaled legacy coverage, so every logical bound, centroid and ground anchor equals
the legacy view's.

    ./scripts/uv run --python 3.12 tests/hires_fixture.py OUTPUT_DIR [--legacy]
"""
import argparse
import json
from pathlib import Path
import shutil
import sys

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/assets"))
from build_slice import INVENTORY, SOURCE  # noqa: E402
from sprite_geometry import logical_bounds  # noqa: E402

# Production densities (assets/slice/README.md).
DENSITIES = {"creatures": 6, "menus": 3, "icons": 6, "meters": 2, "health": 3, "effects": 6,
             "prizes": 2, "battery": 4}
INK = (0x29, 0x1b, 0x35)


def upscale(image, density):
    """Nearest upscale with a soft one-pixel edge; coverage is preserved exactly."""
    big = image.convert("RGBA").resize((image.width * density, image.height * density),
                                       Image.Resampling.NEAREST)
    cover = big.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    inner = cover.filter(ImageFilter.MinFilter(3))
    outer = cover.filter(ImageFilter.MaxFilter(3))
    width = big.width
    data = []
    for i, ((r, g, b, _), c, n, o) in enumerate(zip(big.getdata(), cover.getdata(),
                                                     inner.getdata(), outer.getdata())):
        wobble = (i % width + i // width) & 3
        if c:
            data.append((r ^ wobble, g ^ wobble, b ^ wobble, 255 if n else 192))
        elif o:
            data.append((*INK, 64))
        else:
            data.append((0, 0, 0, 0))
    result = Image.new("RGBA", big.size)
    result.putdata(data)
    return result


def legacy_file(asset):
    """Legacy PNG (relative to SOURCE) behind a manifest entry."""
    if "density" not in asset:
        return asset["path"]
    name = Path(asset["path"]).name
    if asset.get("form") == "unicorn":  # No legacy unicorn: Lilac stand-ins.
        name = name.replace("unicorn-", "grown-", 1)
    return f"{asset['kind']}/{name}"


def legacy_manifest():
    """The shipped manifest with every entry on its legacy PNG and legacy bounds.

    Entries keep IDs, keys and clips; each carries `source_path`, the PNG to read.
    """
    manifest = json.loads((SOURCE / "assets.json").read_text())
    for asset in manifest["assets"]:
        source_path = legacy_file(asset)
        asset.pop("density", None)
        asset["source_path"] = source_path
        # Unicorn stand-ins keep their own file names so paths stay unique.
        unicorn = asset.get("form") == "unicorn"
        asset["path"] = f"creatures/{Path(asset['path']).name}" if unicorn else source_path
        with Image.open(SOURCE / source_path) as image:
            asset["bounds"] = logical_bounds(image.convert("RGBA"))
    return manifest


def make_source(destination, legacy=False, densities=None):
    """Write assets.json + PNGs to `destination`; returns (path, inventory).

    `legacy` writes the legacy view unchanged; otherwise every kind in `densities`
    (default: production densities) is upscaled into `hires/<kind>/`.
    """
    densities = {} if legacy else DENSITIES if densities is None else densities
    destination = Path(destination)
    manifest = legacy_manifest()
    for asset in manifest["assets"]:
        original = SOURCE / asset.pop("source_path")
        density = densities.get(asset["kind"])
        if density is not None:
            asset["path"] = f"hires/{asset['kind']}/{Path(asset['path']).name}"
            asset["density"] = density
        target = destination / asset["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        if density is None:
            shutil.copyfile(original, target)
            continue
        with Image.open(original) as image:
            upscale(image, density).save(target)
    (destination / "assets.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return destination, dict(INVENTORY)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--legacy", action="store_true",
                        help="Write the legacy pixel-art view instead of upscaling it")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == SOURCE.resolve() or SOURCE.resolve() in output.parents:
        parser.error("Refusing to write inside assets/slice")
    _, inventory = make_source(output, args.legacy)
    kind = "legacy" if args.legacy else "high-resolution"
    print(f"Wrote candidate {kind} set ({sum(inventory.values())} assets): {output}")


if __name__ == "__main__":
    main()
