"""Creature data for Jelli Art: palettes, form names and per-pose clip edits.

Clips live in assets/slice/assets.json under "clips", one per creature form and
runtime pose, keyed "<form>.<pose>". The studio may change a clip's frames,
durations and loop flag; clip IDs, keys and the pose list stay fixed because
the game and its tests reference them. The served checkout's
tools/assets/build_slice.py is the authority for what a valid manifest is.
"""
import importlib.util
import json
import shutil
import sys
import tempfile
from pathlib import Path

FRAME_CAP = 6  # build_slice.CLIP_FRAME_CAP; the checkout's value wins when it has one
DURATION_MAX_MS = 10000
_VALIDATORS = {}


class ClipError(ValueError):
    """A clip edit the studio refuses, reported to the page as a 400."""


def palette_colours(manifest, asset):
    """An asset's palette: the shared one, an inline list, or a name in manifest["palettes"]."""
    name = asset.get("palette")
    if name is None:
        return manifest["palette"]
    if isinstance(name, list):
        return name
    return manifest.get("palettes", {}).get(name, manifest["palette"])


def asset_details(manifest):
    """Per-asset fields the review records leave out: pivot, bounds and the resolved palette."""
    details = {}
    for asset in manifest["assets"]:
        name = asset.get("palette")
        details[asset["key"]] = {
            "pivot": asset.get("pivot"), "bounds": asset.get("bounds"),
            "palette": palette_colours(manifest, asset),
            "palette_name": name if isinstance(name, str) else ("inline" if name else "shared")}
    return details


def creature_forms(manifest, pets_path):
    """Manifest creature forms in first-seen order, named from content/pets.json when it says which art each uses."""
    names = {}
    try:
        pets = json.loads(Path(pets_path).read_text())
        for form in pets.get("forms", []):
            if isinstance(form, dict) and isinstance(form.get("art"), str):
                names[form["art"]] = {"name": form.get("name", form["art"]), "id": form.get("id")}
    except (OSError, ValueError):
        pass
    order = []
    for asset in manifest["assets"]:
        if asset["kind"] == "creatures" and asset.get("form") and asset["form"] not in order:
            order.append(asset["form"])
    return [{"art": art, "name": names.get(art, {}).get("name"), "id": names.get(art, {}).get("id")} for art in order]


def creature_data(manifest, pets_path):
    return {"creature_poses": manifest.get("creature_poses", []), "clips": manifest.get("clips", []),
            "palettes": manifest.get("palettes", {}), "creature_forms": creature_forms(manifest, pets_path),
            "clip_frame_cap": FRAME_CAP, "clip_duration_max_ms": DURATION_MAX_MS}


def check_edit(manifest, edit, frame_cap):
    """Readable per-clip errors before the full manifest validation runs."""
    if not isinstance(edit, dict) or not isinstance(edit.get("key"), str):
        raise ClipError("Each clip edit needs a key")
    key = edit["key"]
    clip = next((c for c in manifest.get("clips", []) if c["key"] == key), None)
    if clip is None:
        raise ClipError(f"Unknown clip: {key}")
    frames, durations, loop = edit.get("frames"), edit.get("durations_ms"), edit.get("loop")
    if not isinstance(frames, list) or not 0 < len(frames) <= frame_cap:
        raise ClipError(f"{key}: a clip needs 1 to {frame_cap} frames")
    if not isinstance(durations, list) or len(durations) != len(frames):
        raise ClipError(f"{key}: every frame needs a duration")
    if not all(isinstance(n, int) and not isinstance(n, bool) and 0 < n <= DURATION_MAX_MS for n in durations):
        raise ClipError(f"{key}: durations must be whole milliseconds from 1 to {DURATION_MAX_MS}")
    if not isinstance(loop, bool):
        raise ClipError(f"{key}: loop must be true or false")
    form = key.split(".", 1)[0]
    assets = {a["key"]: a for a in manifest["assets"]}
    for frame in frames:
        if not isinstance(frame, str) or frame not in assets:
            raise ClipError(f"{key}: unknown frame {frame}")
        if assets[frame]["kind"] != "creatures" or assets[frame].get("form") != form:
            raise ClipError(f"{key}: {frame} does not belong to the {form} form")
    return clip


def apply_edits(manifest, edits, frame_cap=FRAME_CAP):
    """Replace frames, durations and loop of the named clips in place; returns the changed keys."""
    if not isinstance(edits, list) or not edits:
        raise ClipError("No clip edits to save")
    if len({e.get("key") for e in edits if isinstance(e, dict)}) != len(edits):
        raise ClipError("Each clip may appear once per save")
    changed = []
    for edit in edits:
        clip = check_edit(manifest, edit, frame_cap)
        new = {"frames": list(edit["frames"]), "durations_ms": list(edit["durations_ms"]), "loop": edit["loop"]}
        if any(clip[k] != v for k, v in new.items()):
            clip.update(new)
            changed.append(clip["key"])
    return changed


def load_checkout_module(repo, stem):
    """tools/assets/<stem>.py from the served checkout, or None when the checkout has none.

    The container image does not carry these tools, and a hosted checkout moves
    forward, so the module is loaded from the checkout and reloaded when it changes.
    Its siblings (sprite_geometry, build_slice) import from the same directory.
    """
    path = Path(repo) / f"tools/assets/{stem}.py"
    stamp = path.stat().st_mtime_ns if path.exists() else None
    cached = _VALIDATORS.get(path)
    if cached is None or cached[0] != stamp:
        module = None
        if path.exists():
            sys.path.insert(0, str(path.parent))
            try:
                spec = importlib.util.spec_from_file_location(f"jelli_art_{stem}", path)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
            except (ImportError, OSError, SyntaxError):
                module = None
            finally:
                sys.path.remove(str(path.parent))
        _VALIDATORS[path] = (stamp, module)
    return _VALIDATORS[path][1]


def load_validator(repo):
    """The checkout's build_slice module, or None when the checkout has none."""
    return load_checkout_module(repo, "build_slice")


def _run_load_assets(validator, manifest, source):
    """build_slice.load_assets against a scratch copy holding this manifest and the source PNGs."""
    with tempfile.TemporaryDirectory(prefix="jelli-art-clips-") as scratch:
        root = Path(scratch)
        for asset in manifest["assets"]:
            target = root / asset["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path(source) / asset["path"], target)
        (root / "assets.json").write_text(json.dumps(manifest, indent=2) + "\n")
        saved = validator.SOURCE
        validator.SOURCE = root
        try:
            validator.load_assets()
            return None
        except (ValueError, KeyError, TypeError, OSError) as error:
            return str(error) or type(error).__name__
        finally:
            validator.SOURCE = saved


def validate(manifest, original, source, validator):
    """Raise ClipError when the edited manifest fails build_slice; returns a warning or "".

    When the unedited manifest already fails (for example a live paintover with a
    custom colour), clip edits are still checked by build_slice's clip coverage
    rule and check_edit, and the save goes ahead with that warning.
    """
    if validator is None or not hasattr(validator, "load_assets"):
        raise ClipError("This checkout has no tools/assets/build_slice.py, so clip edits cannot be validated")
    error = _run_load_assets(validator, manifest, source)
    if error is None:
        return ""
    baseline = _run_load_assets(validator, original, source)
    if baseline is None:
        raise ClipError(f"The edited clips fail validation: {error}")
    check = getattr(validator, "check_creature_clips", None)
    if check:
        try:
            check(json.loads(json.dumps(manifest)))
        except (ValueError, KeyError) as clip_error:
            raise ClipError(f"The edited clips fail validation: {clip_error}") from clip_error
    return f"Saved, but the manifest already failed validation before this edit: {baseline}"
