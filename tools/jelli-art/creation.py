"""Append-only studio creation, validated by the served checkout's generators."""
import copy
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image
import animation
import creatures
import profiles
import storage


class CreationError(ValueError):
    pass


class StaleCreationError(CreationError):
    pass


KINDS = {"icons": (2001, 3000, (16,)), "props": (3001, 4000, (16, 24, 32)),
         "prizes": (11001, 12000, (32,)), "backgrounds": (10001, 11000, (64,))}
DOCS = ("pets", "creatures", "locations", "prizes", "activities", "behaviors")


def read(path):
    return json.loads(Path(path).read_text())


def data(repo, source, content):
    source, content = Path(source), Path(content)
    manifest = read(source / "assets.json")
    pets, profiles_doc = read(content / "pets.json"), read(content / "creatures.json")
    reservation_sources = [animation.LEDGER, *(p.relative_to(source).as_posix()
                                               for p in sorted((source / "source").glob("*-import.json")))]
    reservations = {path: storage.digest(storage.confined(source, path)) for path in reservation_sources}
    return {"bases": {"manifest": storage.digest(source / "assets.json"),
                      "artwork": storage.digest_bytes(storage.json_bytes({a["path"]: storage.digest(storage.confined(source, a["path"])) for a in manifest["assets"]})),
                      "reservations": storage.digest_bytes(storage.json_bytes(reservations)),
                      **{n: storage.digest(content / f"{n}.json") for n in DOCS}},
            "templates": pets["forms"], "entries": pets["entries"], "evolution_sets": pets["evolution_sets"], "profiles": profiles_doc["profiles"],
            "behaviors": [b["name"] for b in profiles_doc["behaviors"]],
            "palette": manifest["palette"],
            "prizes": read(content / "prizes.json")["prizes"] if (content / "prizes.json").exists() else [],
            "kinds": {k: list(v[2]) for k, v in KINDS.items()}}


def integer(value, label, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise CreationError(f"{label} must be an integer in {minimum}..{maximum}")
    return value


def allocate(used, low, high, label):
    for ident in range(low, high + 1):
        if ident not in used:
            used.add(ident)
            return ident
    raise CreationError(f"No available {label} IDs ({low}..{high}); existing IDs are preserved")


def unique_image(size, palette, ident, occupied=()):
    """A visible paintable starter marker, with a distinct prize silhouette."""
    image = Image.new("RGBA", (size, size))
    color = tuple(bytes.fromhex(palette[0][1:])) + (255,)
    # Encode the allocated ID into the starter's shape so prizes remain distinct.
    for index in range(32):
        if ident & (1 << index):
            image.putpixel((size // 2 - 4 + index % 8, size // 2 - 2 + index // 8), color)
    if image.tobytes() in occupied:
        raise CreationError("Starter marker duplicates artwork; choose another asset ID")
    return image


def append_asset(manifest, images, used, reserved_keys, kind, key, image, **extra):
    ranges = KINDS.get(kind, (1001, 2000, (32, 48)))
    ident = allocate(used, *ranges[:2], f"{kind} artwork")
    path = f"{kind}/{key}.png"
    if f"{kind}.{key}" in reserved_keys or any(a["path"] == path for a in manifest["assets"]):
        raise CreationError("That artwork identity is already used or reserved by frame history")
    reserved_keys.add(f"{kind}.{key}")
    size = image.width
    asset = {"id": ident, "key": f"{kind}.{key}", "path": path, "kind": kind,
             "width": size, "height": size, "pivot": [size // 2, size - 2],
             "bounds": list(image.getchannel("A").getbbox()), **extra}
    manifest["assets"].append(asset)
    images[path] = image
    return asset


def pet_candidate(manifest, docs, images, used, reserved_keys, request, source):
    key, name = request["key"], request["name"]
    pets, config = docs["pets"], docs["creatures"]
    if any(e["name"] == name for e in pets["entries"]) or any(f["art"] == key or f["name"] == name for f in pets["forms"]):
        raise CreationError("Choose a new form identity and name")
    if len(pets["forms"]) >= 8 or len(pets["evolution_sets"]) >= 8:
        raise CreationError("No available saved form or evolution-set IDs")
    template = next((f for f in pets["forms"] if f["art"] == request.get("template_form")), None)
    if template is None:
        raise CreationError("Choose an existing artwork template")
    if request.get("evolution") != "single-form":
        raise CreationError("Choose single-form explicitly; growth and evolution need a separate authored set")
    if request.get("gameplay_policy") != "copy-template":
        raise CreationError("Explicitly choose copying the template activity costs, eligibility, bonuses and behavior repertoire")
    size = integer(request.get("size"), "Creature canvas", 32, 48)
    if size not in (32, 48):
        raise CreationError("Creature canvas must be 32 or 48 pixels")
    if request.get("behavior") not in {b["name"] for b in config["behaviors"]}:
        raise CreationError("Choose an existing behavior template")
    prize_ids = {p["id"] + 1 for p in docs.get("prizes", {}).get("prizes", [])}
    unlock = integer(request.get("unlock_prize"), "Unlock prize", 1, 16)
    if unlock not in prize_ids:
        raise CreationError("Choose an existing collectible unlock prize")
    hint = request.get("hint", "")
    if not isinstance(hint, str) or not re.fullmatch(r"[A-Z0-9 ]{1,24}", hint):
        raise CreationError("Unlock hint needs 1..24 uppercase letters, digits or spaces")
    mapping = {}
    for old in list(manifest["assets"]):
        if old.get("form") != template["art"]:
            continue
        image = Image.open(storage.confined(source, old["path"])).convert("RGBA")
        image = image.resize((size, size), Image.Resampling.NEAREST)
        new = append_asset(manifest, images, used, reserved_keys, "creatures", f"{key}-{old['pose']}", image,
                           form=key, pose=old["pose"], **({"palette": old["palette"]} if "palette" in old else {}))
        mapping[old["key"]] = new
    for old in list(manifest["clips"]):
        if not old["key"].startswith(template["art"] + "."):
            continue
        clip = copy.deepcopy(old)
        clip.update(id=allocate(used, 5001, 6000, "clip"), key=key + old["key"][len(template["art"]):],
                    frames=[mapping[k]["key"] for k in old["frames"]])
        manifest["clips"].append(clip)
    portrait = next((a["id"] for oldkey, a in mapping.items()
                     if next(x for x in manifest["assets"] if x["key"] == oldkey)["id"] == template["portrait"]), None)
    if portrait is None:
        raise CreationError("Template portrait is not part of its artwork")
    form_id, set_id = len(pets["forms"]), len(pets["evolution_sets"]) + 1
    pets["forms"].append({"id": form_id, "name": name, "portrait": portrait, "art": key})
    pets["evolution_sets"].append({"id": set_id, "forms": [form_id], "growth_ticks": 0})
    pets["entries"].append({"id": len(pets["entries"]) + 1, "name": name, "unlock_prize": unlock,
                            "hint": hint, "evolution_set": set_id})
    config["profiles"].append({"art": key, "behavior": request["behavior"],
                               **{f: integer(request.get(f), f, 1, 8) for f in ("scale", "icon_scale", "portrait_scale")}})
    activities = docs["activities"]
    cost = next(c for c in activities["pet_costs"] if c["form"] == template["id"])
    activities["pet_costs"].append({**cost, "form": form_id})
    for moment in activities["moments"]:
        if template["id"] in moment["forms"]:
            moment["forms"].append(form_id)
        for bonus in list(moment["bonuses"]):
            if bonus["form"] == template["id"]:
                moment["bonuses"].append({**bonus, "form": form_id})
    for repertoire in docs["behaviors"]["repertoires"]:
        if template["name"] in repertoire["forms"]:
            repertoire["forms"].append(name)
    return mapping[next(iter(mapping))]["key"]


def validate(repo, source, content, manifest, docs, images):
    validator = creatures.load_validator(repo)
    error = creatures._run_load_assets(validator, manifest, source, images)
    if error:
        raise CreationError(f"Artwork validation failed: {error}")
    with tempfile.TemporaryDirectory(prefix="jelli-create-check-") as directory:
        work = Path(directory)
        shutil.copytree(Path(repo) / "cmake", work / "cmake")
        shutil.copytree(content, work / "content")
        (work / "assets/slice").mkdir(parents=True)
        (work / "assets/slice/assets.json").write_bytes(storage.json_bytes(manifest))
        for name, doc in docs.items():
            (work / "content" / f"{name}.json").write_bytes(storage.json_bytes(doc))
        profiles.validate(docs["creatures"], manifest, creatures.load_checkout_module(repo, "creature_data"),
                          work / "content/pets.json", read(work / "content/behaviors.json"))
        calls = 'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliCollection.cmake")\njelli_collection_data(pets)\n'
        calls += 'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliLocations.cmake")\njelli_locations_data(locations)\n'
        calls += 'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliActivities.cmake")\njelli_activities_data(activities)\n'
        calls += 'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliBehaviors.cmake")\njelli_behaviors_data(behaviors)\n'
        if (work / "cmake/JelliPrizes.cmake").exists():
            calls += 'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliPrizes.cmake")\njelli_prizes_data(prizes)\n'
        (work / "check.cmake").write_text('cmake_minimum_required(VERSION 3.21)\n' + calls)
        result = subprocess.run(["cmake", "-P", str(work / "check.cmake")], cwd=work,
                                capture_output=True, text=True, timeout=30, check=False)
        if result.returncode:
            raise CreationError(result.stderr.strip())


def transactional_write(files):
    """Restore every replaced path when any rename fails (including new PNGs)."""
    temporary_before = {p for path, _ in files for p in path.parent.glob(f".*{storage.TEMP_MARK}*")}
    before = [(path, path.read_bytes() if path.exists() else None) for path, _ in files]
    try:
        storage.write_files(files)
    except BaseException:
        for path, original in before:
            if original is None:
                path.unlink(missing_ok=True)
            elif not path.exists() or path.read_bytes() != original:
                storage.write_file(path, original)
        for path, _ in files:
            for temporary in path.parent.glob(f".*{storage.TEMP_MARK}*"):
                if temporary not in temporary_before:
                    temporary.unlink(missing_ok=True)
        raise


def create(repo, source, content, request, bases):
    """Call under the server's shared WriteLock; returns paths for its git record."""
    source, content = Path(source), Path(content)
    if not isinstance(request, dict) or not isinstance(bases, dict):
        raise CreationError("Creation needs a request and loaded file bases")
    current = data(repo, source, content)["bases"]
    if bases != current:
        raise StaleCreationError("Creation sources changed; reload the New view before creating")
    key, name = request.get("key", ""), request.get("name", "")
    if not isinstance(key, str) or not re.fullmatch(r"[a-z][a-z0-9-]{0,39}", key):
        raise CreationError("Identity needs a lowercase letter followed by letters, digits or hyphens (40 max)")
    if not isinstance(name, str) or not re.fullmatch(r"[A-Z0-9 ]{1,16}", name):
        raise CreationError("Name needs 1..16 uppercase letters, digits or spaces")
    manifest = read(source / "assets.json")
    docs = {n: read(content / f"{n}.json") for n in DOCS if (content / f"{n}.json").exists()}
    original_docs = copy.deepcopy(docs)
    used, reserved_keys = animation.reserved(manifest, source)
    images = {}
    if request.get("type") == "pet":
        selected = pet_candidate(manifest, docs, images, used, reserved_keys, request, source)
    elif request.get("type") == "asset" and request.get("kind") in KINDS:
        kind = request["kind"]
        size = integer(request.get("size"), "Canvas size", 1, 64)
        if size not in KINDS[kind][2]:
            raise CreationError("Unsupported canvas size for that artwork kind")
        palette = request.get("palette", manifest["palette"])
        if not isinstance(palette, list) or not 1 <= len(palette) <= 16 or any(
                not isinstance(c, str) or not re.fullmatch(r"#[0-9a-f]{6}", c) for c in palette) or len(set(palette)) != len(palette):
            raise CreationError("Palette needs 1..16 distinct lowercase #rrggbb colors")
        ident = next((n for n in range(KINDS[kind][0], KINDS[kind][1] + 1) if n not in used), None)
        if ident is None:
            raise CreationError("Artwork ID range is exhausted")
        occupied = [Image.open(storage.confined(source, a["path"])).convert("RGBA").tobytes()
                    for a in manifest["assets"] if a["kind"] == "prizes"] if kind == "prizes" else []
        image = unique_image(size, palette, ident, occupied)
        asset = append_asset(manifest, images, used, reserved_keys, kind, key, image, palette=palette)
        pivot = request.get("pivot", asset["pivot"])
        if not isinstance(pivot, list) or len(pivot) != 2:
            raise CreationError("Placement pivot needs x and y")
        asset["pivot"] = [integer(v, "Pivot", 0, size - 1) for v in pivot]
        selected = asset["key"]
        if request.get("location") is not None:
            location = request["location"]
            if kind != "backgrounds" or not isinstance(location, dict):
                raise CreationError("Only background artwork can create a location")
            places = docs["locations"]["locations"]
            if len(places) >= 8:
                raise CreationError("No available location IDs (capacity 8)")
            if type(location.get("outdoors")) is not bool:
                raise CreationError("Choose indoor or outdoor location explicitly")
            places.append({"id": len(places), "key": location.get("key"), "name": location.get("name"),
                           "background": asset["id"], "outdoors": location["outdoors"]})
        if request.get("collectible") is True:
            if kind != "prizes" or "prizes" not in docs:
                raise CreationError("Collectible registration requires prize artwork and a prize registry")
            prizes = docs["prizes"]["prizes"]
            if len(prizes) >= 16:
                raise CreationError("No available collectible IDs (capacity 16)")
            prizes.append({"id": len(prizes), "key": key, "name": name, "asset": asset["id"]})
    else:
        raise CreationError("Choose pet or a supported artwork kind")
    try:
        validate(repo, source, content, manifest, docs, images)
    except (ValueError, KeyError, TypeError) as error:
        raise CreationError(str(error)) from error
    for path in images:
        if storage.confined(source, path).exists():
            raise CreationError(f"Artwork file already exists: {path}; choose another identity")
    files = [(storage.confined(source, path), storage.png_bytes(image)) for path, image in images.items()]
    files += [(storage.confined(content, f"{n}.json"), storage.json_bytes(doc)) for n, doc in docs.items() if doc != original_docs[n]]
    files.append((storage.confined(source, "assets.json"), storage.json_bytes(manifest)))
    if data(repo, source, content)["bases"] != current:
        raise StaleCreationError("Creation sources changed during validation; reload before creating")
    transactional_write(files)
    return {"ok": True, "key": selected, "paths": [str(p) for p, _ in files],
            "creation": data(repo, source, content)}
