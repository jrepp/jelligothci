"""New and retired creature frames for Jelli Art's animation timeline.

A new frame is a new PNG plus a manifest asset of kind "creatures" that copies
its form's size, pivot and palette. IDs are append-only: a frame takes the next
free ID in its form's range and existing IDs never move. A form imported from
source/<name>-import.json numbers its frames first_id + list position, so a new
studio frame is appended to that spec as a {"studio": true} entry and takes
exactly that slot (the importer keeps such entries). Every frame the studio adds
or retires is also recorded in source/studio-frames.json, so a retired ID is
never handed out again. Retiring removes the asset and its PNG; a frame that a
clip, a form portrait or content/creatures.json still uses is refused.

The served checkout's build_slice.load_assets() checks a scratch copy before
anything is written.
"""
import json

import storage
import re
from pathlib import Path

from PIL import Image

import creatures

LEDGER = "source/studio-frames.json"
LEDGER_NOTE = ("Creature frames added or retired in Jelli Art. Append-only: an ID listed here is never reused, "
               "and a retired frame keeps its entry.")
POSE_NAME = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
POSE_MAX = 40
DEFAULT_MS = 450
# tools/assets/live_assets.py and build_slice.py limits; the checkout's values win when present.
LIVE_LIMITS = {"MAX_ASSETS": 192, "LIVE_PIXEL_CAPACITY": 196608, "LIVE_MASK_CAPACITY": 24576}


class FrameError(ValueError):
    """A frame request the studio refuses, reported to the page as a 400."""


def read_json(path, default):
    path = Path(path)
    return json.loads(path.read_text()) if path.exists() else default


def write_json(path, value):
    """Atomic, fsynced replace (storage.py), so neither the game watcher nor a crash sees half a file."""
    storage.write_file(Path(path), storage.json_bytes(value))


def ledger(source):
    data = read_json(Path(source) / LEDGER, {})
    return {"note": data.get("note", LEDGER_NOTE), "frames": list(data.get("frames", []))}


def import_spec(source, form):
    """(path, spec) of the creature import spec for this form, or (None, None)."""
    for path in sorted((Path(source) / "source").glob("*-import.json")):
        try:
            spec = json.loads(path.read_text())
        except ValueError:
            continue
        if spec.get("kind", "creatures") == "creatures" and spec.get("form") == form:
            return path, spec
    return None, None


def form_assets(manifest, form):
    return [a for a in manifest["assets"] if a["kind"] == "creatures" and a.get("form") == form]


def reserved(manifest, source):
    """Every ID and creature key that is taken or was ever handed out."""
    ids = {a["id"] for a in manifest["assets"]} | {c["id"] for c in manifest.get("clips", [])}
    keys = {a["key"] for a in manifest["assets"]}
    for entry in ledger(source)["frames"]:
        ids.add(entry["id"])
        keys.add(entry["key"])
    for path in sorted((Path(source) / "source").glob("*-import.json")):
        try:
            spec = json.loads(path.read_text())
        except ValueError:
            continue
        kind, name = spec.get("kind", "creatures"), spec.get("form") or spec.get("name")
        ids.update(range(spec["first_id"], spec["first_id"] + len(spec.get("frames", []))))
        keys.update(f"{kind}.{name}-{f['pose']}" for f in spec.get("frames", []) if "pose" in f)
    return ids, keys


def next_frame_id(manifest, source, form):
    """The ID a new frame of this form gets: appended, never filling an old gap."""
    mine = [a["id"] for a in form_assets(manifest, form)]
    if not mine:
        raise FrameError(f"Unknown creature form: {form}")
    ids, _ = reserved(manifest, source)
    _, spec = import_spec(source, form)
    if spec is not None:
        ident = spec["first_id"] + len(spec["frames"])
        others = {a["id"] for a in manifest["assets"]} | {c["id"] for c in manifest.get("clips", [])}
        others |= {e["id"] for e in ledger(source)["frames"]}
        if ident in others:
            raise FrameError(f"ID {ident}, the next slot in the {form} import spec, is already taken")
        return ident
    low = min(mine) // 100 * 100
    high = low + 100
    ident = max(i for i in ids if low <= i < high) + 1
    while ident in ids:
        ident += 1
    if ident >= high:
        raise FrameError(f"The {form} form has no free ID left in {low}..{high - 1}")
    return ident


def pose_name(manifest, source, form, requested, base):
    """A pose name that gives an unused key: the requested one, else base, base-2, base-3..."""
    _, keys = reserved(manifest, source)
    if requested:
        if not isinstance(requested, str) or not POSE_NAME.match(requested) or len(requested) > POSE_MAX:
            raise FrameError(f"Frame names use lowercase letters, digits and single hyphens (at most {POSE_MAX})")
        if f"creatures.{form}-{requested}" in keys:
            raise FrameError(f"creatures.{form}-{requested} exists or was used before; choose another name")
        return requested
    for n in range(1, 1000):
        name = base if n == 1 else f"{base}-{n}"
        if f"creatures.{form}-{name}" not in keys and len(name) <= POSE_MAX:
            return name
    raise FrameError("No free frame name")


def darkest(colours):
    def luma(hex_colour):
        r, g, b = bytes.fromhex(hex_colour[1:])
        return 299 * r + 587 * g + 114 * b
    return min(colours, key=luma)


def blank_image(manifest, template):
    """A frame with one ink pixel at the pivot: build_slice needs an opaque pixel, and it keeps the ground anchor."""
    image = Image.new("RGBA", (template["width"], template["height"]), (0, 0, 0, 0))
    ink = darkest(creatures.palette_colours(manifest, template))
    image.putpixel(tuple(template["pivot"]), tuple(bytes.fromhex(ink[1:])) + (255,))
    return image


def check_budget(manifest, validator, live):
    limits = {name: getattr(live, name, None) or getattr(validator, name, None) or value
              for name, value in LIVE_LIMITS.items()}
    sprites = [a for a in manifest["assets"] if a["kind"] != "font"]
    pixels = sum(a["width"] * a["height"] for a in sprites)
    masks = sum((a["width"] + 7) // 8 * a["height"] for a in sprites)
    if len(manifest["assets"]) > limits["MAX_ASSETS"]:
        raise FrameError(f"The live pack holds at most {limits['MAX_ASSETS']} assets")
    if pixels > limits["LIVE_PIXEL_CAPACITY"] or masks > limits["LIVE_MASK_CAPACITY"]:
        raise FrameError(f"Art would need {pixels} pixels / {masks} mask bytes; the live banks hold "
                         f"{limits['LIVE_PIXEL_CAPACITY']} / {limits['LIVE_MASK_CAPACITY']}")


def insert_into_clip(manifest, key, frame_key, index, duration, frame_cap):
    """Insert frame_key into an existing clip of its form; returns the clip key."""
    clip = next((c for c in manifest.get("clips", []) if c["key"] == key), None)
    if clip is None:
        raise FrameError(f"Unknown clip: {key}; add the clip first")
    frames, durations = list(clip["frames"]), list(clip["durations_ms"])
    index = len(frames) if index is None else index
    if not isinstance(index, int) or isinstance(index, bool) or not 0 <= index <= len(frames):
        raise FrameError(f"{key}: insert position must be 0 to {len(frames)}")
    if duration is None:
        duration = durations[min(index, len(durations) - 1)] if durations else DEFAULT_MS
    frames.insert(index, frame_key)
    durations.insert(index, duration)
    try:
        creatures.apply_edits(manifest, [{"key": key, "frames": frames, "durations_ms": durations,
                                          "loop": clip["loop"]}], frame_cap)
    except creatures.ClipError as error:
        raise FrameError(str(error)) from error
    return key


def plan_add(manifest, source, request, frame_cap):
    """The candidate manifest change for a new frame: (asset, image, spec entry or None, clip key or None)."""
    form, origin = request.get("form"), request.get("from")
    frames = form_assets(manifest, form) if isinstance(form, str) else []
    if not frames:
        raise FrameError(f"Unknown creature form: {form}")
    if origin is not None:
        template = next((a for a in frames if a["key"] == origin), None)
        if template is None:
            raise FrameError(f"{origin} is not a {form} frame")
        image = Image.open(Path(source) / template["path"]).convert("RGBA")
        base = f"{template.get('pose') or 'frame'}-copy"
    else:
        template = min(frames, key=lambda a: a["id"])
        image = blank_image(manifest, template)
        base = "frame"
    pose = pose_name(manifest, source, form, request.get("pose"), base)
    asset = {"id": next_frame_id(manifest, source, form), "key": f"creatures.{form}-{pose}",
             "path": f"creatures/{form}-{pose}.png", "kind": "creatures",
             "width": template["width"], "height": template["height"], "pivot": list(template["pivot"]),
             "bounds": list(image.getchannel("A").getbbox()), "form": form, "pose": pose}
    if "palette" in template:
        asset["palette"] = template["palette"]
    if (Path(source) / asset["path"]).exists():
        raise FrameError(f"{asset['path']} already exists on disk; choose another name")
    manifest["assets"].append(asset)
    clip = None
    if request.get("clip") is not None:
        clip = insert_into_clip(manifest, request["clip"], asset["key"], request.get("index"),
                                request.get("duration_ms"), frame_cap)
    return asset, image, clip


def frame_users(manifest, key, pets_path, creature_data_path):
    """Why a frame cannot be retired: clips, portraits and content that name it."""
    users = [f"clip {c['key']}" for c in manifest.get("clips", []) if key in c["frames"]]
    asset = next((a for a in manifest["assets"] if a["key"] == key), None)
    pets = read_json(pets_path, {})
    for form in pets.get("forms", []) if isinstance(pets, dict) else []:
        if isinstance(form, dict) and asset and form.get("portrait") == asset["id"]:
            users.append(f"the {form.get('name', form.get('art'))} portrait in content/pets.json")
    path = Path(creature_data_path)
    if path.exists() and f'"{key}"' in path.read_text():
        users.append("content/creatures.json")
    return users


def record_frame(source, entry):
    data = ledger(source)
    data["frames"].append(entry)
    write_json(Path(source) / LEDGER, data)
    return Path(source) / LEDGER


def mark_hand_painted(source, key, add):
    path = Path(source) / "source/hand-painted.json"
    painted = read_json(path, [])
    updated = sorted({*painted, key}) if add else [k for k in painted if k != key]
    if updated != painted:
        write_json(path, updated)
        return [path]
    return []


def add_frame(source, repo, request):
    """Validate, then write a new frame's PNG, asset, ledger entry and optional clip insertion.

    Returns (result, written paths, commit subject).
    """
    if not isinstance(request, dict):
        raise FrameError("Send a frame request object")
    manifest_path = Path(source) / "assets.json"
    original = json.loads(manifest_path.read_text())
    manifest = json.loads(json.dumps(original))
    validator = creatures.load_validator(repo)
    cap = getattr(validator, "CLIP_FRAME_CAP", creatures.FRAME_CAP)
    asset, image, clip = plan_add(manifest, source, request, cap)
    check_budget(manifest, validator, creatures.load_checkout_module(repo, "live_assets"))
    try:
        warning = creatures.validate(manifest, original, source, validator, {asset["path"]: image})
    except creatures.ClipError as error:
        raise FrameError(str(error)) from error
    target = Path(source) / asset["path"]
    storage.write_file(target, storage.png_bytes(image))
    write_json(manifest_path, manifest)
    written = [target, manifest_path, *mark_hand_painted(source, asset["key"], True)]
    origin = request.get("from")
    written.append(record_frame(source, {"id": asset["id"], "key": asset["key"], "form": asset["form"],
                                         "from": origin, "added": "Jelli Art"}))
    spec_path, spec = import_spec(source, asset["form"])
    if spec is not None:
        spec["frames"].append({"pose": asset["pose"], "studio": True,
                               "note": "Added in Jelli Art; the importer keeps this slot and leaves the PNG alone."})
        write_json(spec_path, spec)
        written.append(spec_path)
    how = f"duplicate {origin} as" if origin else "add blank frame"
    subject = f"chore(art): {how} {asset['key']}{f' in {clip}' if clip else ''} in Jelli Art"
    result = {"key": asset["key"], "id": asset["id"], "clip": clip, **({"warning": warning} if warning else {})}
    return result, written, subject


def retire_frame(source, repo, key, pets_path, creature_data_path):
    """Remove an unused frame's asset and PNG; its ID and key stay reserved in the ledger."""
    manifest_path = Path(source) / "assets.json"
    original = json.loads(manifest_path.read_text())
    asset = next((a for a in original["assets"] if a["key"] == key), None)
    if asset is None or asset["kind"] != "creatures" or not asset.get("form"):
        raise FrameError(f"{key} is not a creature frame")
    users = frame_users(original, key, pets_path, creature_data_path)
    if users:
        raise FrameError(f"{key} is still used by {', '.join(users)}; remove it there first")
    manifest = json.loads(json.dumps(original))
    manifest["assets"] = [a for a in manifest["assets"] if a["key"] != key]
    try:
        warning = creatures.validate(manifest, original, source, creatures.load_validator(repo))
    except creatures.ClipError as error:
        raise FrameError(str(error)) from error
    png = Path(source) / asset["path"]
    write_json(manifest_path, manifest)
    png.unlink(missing_ok=True)
    written = [png, manifest_path, *mark_hand_painted(source, key, False)]
    written.append(record_frame(source, {"id": asset["id"], "key": key, "form": asset["form"], "retired": True}))
    spec_path, spec = import_spec(source, asset["form"])
    entry = next((f for f in (spec or {}).get("frames", []) if f"creatures.{asset['form']}-{f.get('pose')}" == key), None)
    if entry is not None and not entry.get("retired"):
        entry.update(retired=True, note="Retired in Jelli Art; kept so later IDs do not shift.")
        write_json(spec_path, spec)
        written.append(spec_path)
    result = {"key": key, "id": asset["id"], **({"warning": warning} if warning else {})}
    return result, written, f"chore(art): retire unused frame {key} in Jelli Art"


def frame_overview(manifest, source, pets_path, creature_data_path):
    """Per form: the next frame ID, each frame's users, and retired frames, for the timeline page."""
    out = {}
    retired = [e for e in ledger(source)["frames"] if e.get("retired")]
    for form in dict.fromkeys(a["form"] for a in manifest["assets"] if a["kind"] == "creatures" and a.get("form")):
        try:
            ident = next_frame_id(manifest, source, form)
        except FrameError:
            ident = None
        spec_path, _ = import_spec(source, form)
        out[form] = {"next_id": ident, "import_spec": spec_path.name if spec_path else None,
                     "frames": {a["key"]: frame_users(manifest, a["key"], pets_path, creature_data_path)
                                for a in form_assets(manifest, form)},
                     "retired": [e for e in retired if e.get("form") == form]}
    return out
