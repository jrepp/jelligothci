# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Build a self-contained before/after pixel review page for slice artwork.

"Before" is a git revision (default HEAD) or a directory laid out like
assets/slice. "After" is a directory (default the working tree's assets/slice).
"""
import argparse
import base64
import functools
import io
import json
import os
import subprocess
from pathlib import Path

from PIL import Image

import lint_rules

# JELLI_REPO points the tools at another checkout (the Jelli Art container mounts one).
REPO = Path(os.environ.get("JELLI_REPO") or Path(__file__).resolve().parents[2]).resolve()
SOURCE = REPO / "assets/slice"
INK = (0x29, 0x1B, 0x35)
WHITE = (0xFF, 0xFF, 0xFF)
# On-device draw scale per asset kind (see core/pet_layout.c, pet_draw.c, pet_gallery.c).
DEVICE_SCALE = {"creatures": 6, "icons": 6, "menus": 3, "meters": 2, "health": 3, "effects": 2,
                "prizes": 2, "props": 3, "font": 2, "backgrounds": 466 / 64}
# Kinds whose silhouettes are expected to carry a closed ink outline.
OUTLINED = {"creatures", "icons", "menus", "meters", "health", "effects", "prizes", "props"}


def read_before(spec, relative):
    """PNG bytes for a manifest path from a directory or git revision, or None."""
    folder = Path(spec)
    if folder.is_dir():
        path = folder / relative
        return path.read_bytes() if path.exists() else None
    commit = resolve(spec)
    return git_blob(commit, relative) if commit else None


def resolve(spec):
    """Commit SHA for a revision, so cached blobs stay correct when HEAD moves; None without git."""
    try:
        result = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--verify", f"{spec}^{{commit}}"],
                                capture_output=True, text=True, check=False)
    except OSError:  # git is not installed: there is no history to compare with
        return None
    if result.returncode:
        raise ValueError(f"Unknown revision: {spec}")
    return result.stdout.strip()


@functools.lru_cache(maxsize=4096)
def git_blob(commit, relative):
    result = subprocess.run(["git", "-C", str(REPO), "show", f"{commit}:assets/slice/{relative}"],
                            capture_output=True, check=False)
    return result.stdout if result.returncode == 0 else None


def read_manifest(spec):
    raw = read_before(spec, "assets.json")
    return json.loads(raw) if raw else {"assets": []}


def neighbours(x, y, w, h, diagonal):
    steps = ((-1, 0), (1, 0), (0, -1), (0, 1))
    if diagonal:
        steps += ((-1, -1), (1, -1), (-1, 1), (1, 1))
    for dx, dy in steps:
        nx, ny = x + dx, y + dy
        yield (nx, ny) if 0 <= nx < w and 0 <= ny < h else None


def outline_ink(manifest, asset):
    """The outline colour: shared ink, or the darkest colour of the asset's own palette (lint.js outlineInk)."""
    name = asset.get("palette")
    if name is None:
        return INK
    colours = name if isinstance(name, list) else manifest.get("palettes", {}).get(name, manifest["palette"])
    rgb = [tuple(bytes.fromhex(c[1:])) for c in colours]
    return min(rgb, key=lambda c: c[0] * 299 + c[1] * 587 + c[2] * 114)


def measure(data, kind, ink=INK):
    """Cleanliness signals: isolated specks, unoutlined edge pixels, colour usage.

    ink is the outline colour (outline_ink). Single ink pixels (eyes, mouths) and white
    catchlights are deliberate, so they are not specks. tools/jelli-art/lint.js measure()
    applies the same rules in the page. See docs/pixel-art-guide.md.
    """
    return measure_image(Image.open(io.BytesIO(data)), kind, ink)


def measure_image(image, kind, ink=INK):
    image = image.convert("RGBA")
    w, h = image.size
    px = image.load()
    colors, specks, open_edges = {}, [], []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if not a:
                continue
            hexa = f"#{r:02x}{g:02x}{b:02x}"
            colors[hexa] = colors.get(hexa, 0) + 1
            near = [px[n] if n else (0, 0, 0, 0) for n in neighbours(x, y, w, h, True)]
            if (r, g, b) not in (ink, WHITE) and not any(n[3] and n[:3] == (r, g, b) for n in near):
                specks.append([x, y])
            if kind in OUTLINED and (r, g, b) != ink and any(not n[3] for n in near[:4]):
                open_edges.append([x, y])
    measured = kind not in ("font", "backgrounds")
    return {"colors": colors, "opaque": sum(colors.values()),
            "specks": specks if measured else [], "open_edges": open_edges if kind in OUTLINED else []}


def changed_pixels(before, after):
    a = Image.open(io.BytesIO(before)).convert("RGBA")
    b = Image.open(io.BytesIO(after)).convert("RGBA")
    if a.size != b.size:
        return -1
    pa, pb = list(a.getdata()), list(b.getdata())
    same = lambda p, q: p == q or (p[3] == 0 and q[3] == 0)
    return sum(not same(p, q) for p, q in zip(pa, pb))


def uri(data):
    return "data:image/png;base64," + base64.b64encode(data).decode() if data else None


def collect(before_spec, after_dir):
    manifest = json.loads((after_dir / "assets.json").read_text())
    old = {a["key"]: a for a in read_manifest(before_spec)["assets"]}
    records = []
    for asset in manifest["assets"]:
        after = (after_dir / asset["path"]).read_bytes()
        prior = old.get(asset["key"])
        before = read_before(before_spec, prior["path"]) if prior else None
        record = {k: asset[k] for k in ("key", "id", "kind", "path", "width", "height") if k in asset}
        ink = outline_ink(manifest, asset)
        record.update(form=asset.get("form"), pose=asset.get("pose"), device_scale=DEVICE_SCALE[asset["kind"]],
                      after=uri(after), before=uri(before), after_metrics=measure(after, asset["kind"], ink),
                      before_metrics=measure(before, asset["kind"], ink) if before else None,
                      changed=changed_pixels(before, after) if before else -1)
        records.append(record)
    return manifest, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", default="HEAD", help="git revision or directory (default: HEAD)")
    parser.add_argument("--after", type=Path, default=SOURCE, help="asset directory (default: assets/slice)")
    parser.add_argument("--output", type=Path, default=REPO / "build/assets/compare.html")
    args = parser.parse_args()
    after_dir = args.after.resolve()
    manifest, records = collect(args.before, after_dir)
    label = args.before if not Path(args.before).is_dir() else Path(args.before).resolve().name
    lint, lint_error = lint_rules.load(after_dir, SOURCE, DEVICE_SCALE)
    payload = {"before_label": label, "after_label": after_dir.name if after_dir != SOURCE else "working tree",
               "palette": manifest["palette"], "palette_names": manifest.get("palette_names", {}), "assets": records,
               "lint": lint, "lint_error": lint_error}
    template = Path(__file__).with_name("compare.html").read_text()
    if template.count("__COMPARE_DATA__") != 1:
        raise ValueError("Compare template data marker mismatch")
    # The Jelli Art shell (mode tabs, toasts, themes, shortcut help) works without the studio scripts.
    # lint.js gives the page the same lint verdicts as lint_rules.py.
    studio = Path(__file__).resolve().parent.parent / "jelli-art"
    scripts = [studio / name for name in ("shell.js", "lint.js") if (studio / name).exists()]
    template = template.replace("/*__STUDIO_JS__*/", "\n".join(p.read_text() for p in scripts))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(template.replace("__COMPARE_DATA__", json.dumps(payload).replace("<", "\\u003c")))
    changed = sum(r["changed"] != 0 for r in records)
    failing = sum(bool(lint_rules.verdict(r["key"], r["kind"], r["after_metrics"], lint)["fails"]) for r in records)
    print(f"{changed} of {len(records)} assets differ from {label}; {failing} fail the style lint. Review: {args.output}")
    if lint_error:
        print(f"warning: {lint_error}; using no limits or waivers")


if __name__ == "__main__":
    main()
