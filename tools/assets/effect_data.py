"""Celebration timelines shared by the editor and immutable C asset compiler."""
import json
from scene_data import fields, integer, KEY

MAX_EFFECTS, MAX_TRACKS, MAX_CUES, MAX_POINTS, MAX_DURATION = 32, 16, 16, 8, 30000
LAYERS = ["behind-pet", "front-pet", "foreground"]
POSES = ["idle", "idle-alt", "curious", "content", "eating", "happy", "asleep", "unwell"]


def name(value, label):
    if not isinstance(value, str) or not 1 <= len(value.strip()) <= 48:
        raise ValueError(f"{label} needs 1..48 characters")


def identity(value, seen):
    if not isinstance(value, str) or not KEY.fullmatch(value) or value in seen:
        raise ValueError("Keys must be unique lowercase identities")
    seen.add(value)


def bounded_list(value, low, high, label):
    if not isinstance(value, list) or not low <= len(value) <= high:
        raise ValueError(f"{label} needs {low}..{high} entries")


def interval(item, duration):
    integer(item["start_ms"], 0, duration - 1, "Start")
    integer(item["duration_ms"], 1, duration - item["start_ms"], "Duration")


def validate(doc, manifest):
    fields(doc, "version hardpoints effects", "Effect document")
    integer(doc["version"], 1, 1, "Version")
    forms = {a["form"] for a in manifest["assets"] if a["kind"] == "creatures"}
    assets = {a["key"]: a for a in manifest["assets"]}
    bounded_list(doc["hardpoints"], 1, MAX_POINTS, "Hardpoints")
    points = {"screen"}
    for point in doc["hardpoints"]:
        fields(point, "key name positions", "Hardpoint")
        identity(point["key"], points)
        name(point["name"], "Hardpoint name")
        if not isinstance(point["positions"], dict) or set(point["positions"]) != forms:
            raise ValueError("Each hardpoint needs a position for every creature art form")
        for xy in point["positions"].values():
            bounded_list(xy, 2, 2, "Hardpoint position")
            for v in xy:
                integer(v, -64, 64, "Hardpoint source-pixel offset")
    bounded_list(doc["effects"], 1, MAX_EFFECTS, "Effects")
    keys = set()
    for effect in doc["effects"]:
        fields(effect, "key name duration_ms tracks cues", "Effect")
        identity(effect["key"], keys)
        name(effect["name"], "Effect name")
        integer(effect["duration_ms"], 1, MAX_DURATION, "Effect duration")
        bounded_list(effect["tracks"], 0, MAX_TRACKS, "Sprite tracks")
        bounded_list(effect["cues"], 0, MAX_CUES, "Creature cues")
        for track in effect["tracks"]:
            fields(track, "name layer anchor start_ms duration_ms x y end_x end_y scale easing loop frames", "Track")
            name(track["name"], "Track name")
            interval(track, effect["duration_ms"])
            if track["layer"] not in LAYERS or not isinstance(track["anchor"], str) or track["anchor"] not in points:
                raise ValueError("Choose a registered layer and attachment")
            if track["easing"] not in ("linear", "smooth") or type(track["loop"]) is not bool:
                raise ValueError("Choose linear/smooth easing and a boolean loop")
            for key in ("x", "y", "end_x", "end_y"):
                integer(track[key], -512, 512, key)
            integer(track["scale"], 1, 8, "Sprite scale")
            bounded_list(track["frames"], 1, 6, "Sprite frames")
            for frame in track["frames"]:
                fields(frame, "asset duration_ms", "Sprite frame")
                asset = assets.get(frame["asset"]) if isinstance(frame["asset"], str) else None
                if not asset or asset["kind"] not in ("effects", "props", "icons"):
                    raise ValueError("Sprite frames must reference effects, props or icons")
                integer(frame["duration_ms"], 1, MAX_DURATION, "Frame hold")
        for cue in effect["cues"]:
            fields(cue, "name pose start_ms duration_ms", "Creature cue")
            name(cue["name"], "Cue name")
            interval(cue, effect["duration_ms"])
            if cue["pose"] not in POSES:
                raise ValueError("Choose a base creature pose")
        previous_end = 0
        for cue in sorted(effect["cues"], key=lambda c: c["start_ms"]):
            if cue["start_ms"] < previous_end:
                raise ValueError("Creature cues cannot overlap; sprite tracks can")
            previous_end = cue["start_ms"] + cue["duration_ms"]
    return doc


def initial(manifest):
    forms = sorted({a["form"] for a in manifest["assets"] if a["kind"] == "creatures"})
    points = [("ground", "Ground", [0, 0]), ("head", "Head", [0, -20]),
              ("mouth", "Mouth", [0, -9]), ("left-hand", "Left hand", [-10, -8]),
              ("right-hand", "Right hand", [10, -8]), ("chest", "Chest", [0, -7])]
    return {"version": 1, "hardpoints": [{"key": k, "name": n, "positions": {f: list(xy) for f in forms}}
                                         for k, n, xy in points],
            "effects": [{"key": "celebrate", "name": "Celebration", "duration_ms": 3000,
                         "tracks": [], "cues": [{"name": "Happy", "pose": "happy", "start_ms": 0, "duration_ms": 3000}]}]}


def emit(doc, manifest, forms):
    validate(doc, manifest)
    assets = {a["key"]: a["id"] for a in manifest["assets"]}
    points = ["screen"] + [p["key"] for p in doc["hardpoints"]]
    out = ['#include "jelli/effect.h"', 'const JelliEffectDefinition jelli_effect_definitions[] = {']
    for e in doc["effects"]:
        out.append(f'{{.key={json.dumps(e["key"])}, .duration_ms={e["duration_ms"]}, .track_count={len(e["tracks"])}, .cue_count={len(e["cues"])},')
        out.append('.tracks={')
        for t in e["tracks"]:
            frames = ','.join(str(assets[f["asset"]]) for f in t["frames"])
            holds = ','.join(str(f["duration_ms"]) for f in t["frames"])
            out.append('{.clip={.frames={' + frames + '},.durations_ms={' + holds + '},' +
                       f'.count={len(t["frames"])},.loop={str(t["loop"]).lower()}' + '},' +
                       ','.join(f'.{k}={t[k]}' for k in ('start_ms','duration_ms','x','y','end_x','end_y','scale')) +
                       f',.layer={LAYERS.index(t["layer"])},.anchor={points.index(t["anchor"])},.ease={str(t["easing"]=="smooth").lower()}' + '},')
        if not e["tracks"]:
            out.append('{0}')
        out.append('},.cues={')
        out.extend(f'{{{c["start_ms"]},{c["duration_ms"]},{POSES.index(c["pose"])}}},' for c in e["cues"])
        if not e["cues"]:
            out.append('{0}')
        out.append('}},')
    out += ['};', f'const unsigned jelli_effect_definition_count={len(doc["effects"])}u;']
    out.append('JelliEffectPoint jelli_effect_point(unsigned form, unsigned anchor) {')
    out.append(f'static const JelliEffectPoint points[{len(forms)}][{len(points)}] = {{')
    for form in sorted(forms, key=lambda f: f['id']):
        values = ['{0,0}'] + ['{' + ','.join(map(str,p['positions'][form['art']])) + '}' for p in doc['hardpoints']]
        out.append('{' + ','.join(values) + '},')
    out += ['};', f'if(form>={len(forms)}u || anchor>={len(points)}u) return (JelliEffectPoint){{0}};',
            'return points[form][anchor];', '}']
    return '\n'.join(out)
