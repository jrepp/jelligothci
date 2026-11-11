"""Shared scene validation and immutable runtime table generation."""
import json
import re

VERSION = 1
MAX_SCENES = 32
MAX_LAYERS = 12
KEY = re.compile(r"[a-z][a-z0-9_-]{0,47}")


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{label} must be an integer from {low} to {high}")


def fields(value, required, label, optional=""):
    if (not isinstance(value, dict) or not set(required.split()) <= set(value) or
            set(value) - set(required.split()) - set(optional.split())):
        raise ValueError(f"{label} needs exactly: {required}")


def validate(doc, manifest):
    fields(doc, "version scenes", "Scene document")
    if type(doc["version"]) is not int or doc["version"] != VERSION:
        raise ValueError("Unsupported scene document version")
    if not isinstance(doc["scenes"], list) or not 1 <= len(doc["scenes"]) <= MAX_SCENES:
        raise ValueError(f"Choose 1..{MAX_SCENES} scenes")
    assets = {a["key"]: a for a in manifest["assets"]}
    keys = set()
    activities = set()
    for scene_index, scene in enumerate(doc["scenes"], 1):
        try:
            fields(scene, "key name fill layers", "Scene", "seed activity speed")
            if "activity" in scene or "speed" in scene:
                activity = scene.get("activity")
                if (not isinstance(activity, str) or not re.fullmatch(r"[A-Z0-9 ]{1,12}", activity)
                        or activity in activities):
                    raise ValueError("Activity bindings need unique stable activity names")
                activities.add(activity)
                integer(scene.get("speed"), 1, 32, "Activity travel speed")
            integer(scene.get("seed", 0), 0, 65535, "Scene seed")
            if not isinstance(scene["key"], str) or not KEY.fullmatch(scene["key"]) or scene["key"] in keys:
                raise ValueError("Scene keys must be unique lowercase identities")
            keys.add(scene["key"])
            if not isinstance(scene["name"], str) or not 1 <= len(scene["name"].strip()) <= 48:
                raise ValueError("Scene names need 1..48 characters")
            if not isinstance(scene["fill"], str) or not re.fullmatch(r"#[0-9a-f]{6}", scene["fill"]):
                raise ValueError("Scene fill needs a lowercase #rrggbb color")
            if not isinstance(scene["layers"], list) or not 1 <= len(scene["layers"]) <= MAX_LAYERS:
                raise ValueError(f"Choose 1..{MAX_LAYERS} layers")
            for layer_index, layer in enumerate(scene["layers"], 1):
                try:
                    fields(layer, "asset x y tilt_x tilt_y travel repeat_x visible", "Layer", "name random")
                    if "name" in layer and (not isinstance(layer["name"], str) or
                                            not layer["name"].strip() or len(layer["name"]) > 48):
                        raise ValueError("Layer names need 1..48 characters")
                    if "random" in layer:
                        variation = layer["random"]
                        fields(variation, "seed x y travel chance", "Layer variation")
                        integer(variation["seed"], 0, 65535, "Layer seed")
                        for axis in ("x", "y"):
                            integer(variation[axis], 0, 16, "Position variation")
                        integer(variation["travel"], 0, 100, "Travel variation")
                        integer(variation["chance"], 0, 100, "Appearance chance")
                        if variation["travel"] and not layer["repeat_x"]:
                            raise ValueError("Travel variation requires horizontal repeat")
                    if not isinstance(layer["asset"], str):
                        raise ValueError("Choose a background asset")
                    asset = assets.get(layer["asset"])
                    if not asset or asset["kind"] != "backgrounds":
                        raise ValueError("Layers must reference registered background artwork")
                    integer(asset["width"], 1, 256, "Layer width")
                    integer(asset["height"], 1, 256, "Layer height")
                    for name in ("x", "y"):
                        integer(layer[name], -256, 256, name)
                    for name in ("tilt_x", "tilt_y"):
                        integer(layer[name], 0, 16, name)
                    integer(layer["travel"], 0, 100, "Travel percent")
                    for name in ("repeat_x", "visible"):
                        if type(layer[name]) is not bool:
                            raise ValueError(f"{name} must be true or false")
                    if layer["travel"] and not layer["repeat_x"]:
                        raise ValueError("Travel layers must repeat horizontally")
                except ValueError as error:
                    raise ValueError(f"Layer {layer_index}: {error}") from error
        except ValueError as error:
            name = scene.get("name", "Unnamed") if isinstance(scene, dict) else "Invalid scene"
            raise ValueError(f"Scene {scene_index} ({name}): {error}") from error
    return doc


def validate_bindings(doc, moments):
    names = {m["name"] for m in moments} | {"WORKOUT"}
    for scene in doc["scenes"]:
        if "activity" in scene and scene["activity"] not in names:
            raise ValueError(f"Scene {scene['name']}: unknown activity {scene['activity']}")


def initial(manifest):
    """Older checkouts can start from their existing flattened backgrounds."""
    assets = [a for a in manifest["assets"] if a["kind"] == "backgrounds"]
    return {"version": VERSION, "scenes": [
        {"key": "first-scene", "name": "First scene", "fill": "#30312b", "layers": [
            {"asset": assets[0]["key"], "x": 0, "y": 0, "tilt_x": 0, "tilt_y": 0,
             "travel": 0, "repeat_x": False, "visible": True}]}]} if assets else None


def resolve_layer(scene, layer):
    """Seeded initial variation; matching integer algorithm lives in scene_variation.js.

    Stable per-layer seeds make variation independent of names, asset keys and order.
    Resolve at build time so the portable renderer needs no RNG or new state.
    """
    result = dict(layer)
    variation = layer.get("random")
    if variation is None:
        return result
    state = (scene.get("seed", 0) << 16) | variation["seed"]

    def sample():
        nonlocal state
        state = (state * 1664525 + 1013904223) & 0xffffffff
        return state >> 8

    for field, limit in (("x", 256), ("y", 256), ("travel", 100)):
        spread = variation[field]
        value = layer[field] + sample() % (2 * spread + 1) - spread
        result[field] = max(0 if field == "travel" else -limit, min(limit, value))
    result["visible"] = layer["visible"] and sample() % 100 < variation["chance"]
    return result


def emit(doc, manifest):
    """Resolve keys once at build time; the renderer never parses JSON."""
    validate(doc, manifest)
    assets = {a["key"]: a for a in manifest["assets"]}
    lines = ['#include "jelli/scene.h"', 'const JelliSceneDefinition jelli_scene_definitions[] = {']
    for scene in doc["scenes"]:
        r, g, b = bytes.fromhex(scene["fill"][1:])
        fill = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        lines.append(f'    {{.key = {json.dumps(scene["key"])}, .fill = {fill}u, '
                     f'.count = {len(scene["layers"])}u, .layers = {{')
        for authored in scene["layers"]:
            layer = resolve_layer(scene, authored)
            asset = assets[layer["asset"]]
            values = [f'{asset["id"]}u', f'{asset["width"]}u', f'{asset["height"]}u',
                      str(layer["x"]), str(layer["y"]), f'{layer["tilt_x"]}u',
                      f'{layer["tilt_y"]}u', f'{layer["travel"]}u',
                      str(layer["repeat_x"]).lower(), str(layer["visible"]).lower()]
            lines.append("        {" + ", ".join(values) + "},")
        binding = json.dumps(scene["activity"]) if "activity" in scene else "NULL"
        lines.append(f'    }}, .activity = {binding}, .speed = {scene.get("speed", 0)}u}},')
    lines.extend(["};", f'const unsigned jelli_scene_definition_count = {len(doc["scenes"])}u;'])
    return "\n".join(lines)


def resources(doc, manifest):
    """Unique decoded pixels/masks per scene, excluding already embedded tables."""
    validate(doc, manifest)
    assets = {a["key"]: a for a in manifest["assets"]}
    result = {}
    for scene in doc["scenes"]:
        keys = sorted({layer["asset"] for layer in scene["layers"]})
        size = sum(assets[k]["width"] * assets[k]["height"] * 2 +
                   ((assets[k]["width"] + 7) // 8) * assets[k]["height"] for k in keys)
        result[scene["key"]] = {"assets": keys, "decoded_bytes": size}
    return result
