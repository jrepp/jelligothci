"""Validate and emit per-form creature presentation as C tables.

Inputs: content/pets.json forms (ID order, `art` names), assets.json clips,
and content/creatures.json profiles and behaviours. Output: the clip table and
profile table declared in include/jelli/creature.h.
"""
import json
from pathlib import Path

from build_slice import CREATURE_POSES

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "content/pets.json"
CREATURES = ROOT / "content/creatures.json"
BEHAVIORS = ROOT / "content/behaviors.json"
CAPTION_LIMIT = 16
# Order matches JelliCreatureCondition in include/jelli/creature.h.
CREATURE_CONDITIONS = ("asleep", "wake_groggy", "wake_surprised", "wake_happy", "unwell",
                       "eating", "playing", "touch_happy", "touch_upset")
IDLE_POSES = CREATURE_POSES[:4]
RULE_CAPACITY = 12
BEAT_CAPACITY = 16
FLOOR_Y = 256  # Home contact row in core/pet_actor.c.
HEADING_BOTTOM = 80  # Name and location headings sit above this row.
MAX_ACTOR_WIDTH = 300  # Keeps the actor inside the round panel at the floor row.


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_behavior(behavior, poses):
    """poses: base poses then state poses; a rule may pick either (a state pose falls back per form)."""
    name = behavior["name"]
    rules = behavior["pose_rules"]
    require(0 < len(rules) <= RULE_CAPACITY, f"Behaviour {name}: 1..{RULE_CAPACITY} pose rules")
    seen = set()
    for rule in rules:
        require(rule["when"] in CREATURE_CONDITIONS, f"Behaviour {name}: unknown condition {rule['when']}")
        require(rule["pose"] in poses, f"Behaviour {name}: unknown pose {rule['pose']}")
        require(rule["when"] not in seen, f"Behaviour {name}: {rule['when']} listed twice")
        seen.add(rule["when"])
    beats = behavior["idle_beats"]
    require(0 < len(beats) <= BEAT_CAPACITY, f"Behaviour {name}: 1..{BEAT_CAPACITY} idle beats")
    require(all(b in IDLE_POSES for b in beats), f"Behaviour {name}: idle beats use {IDLE_POSES}")
    quiet = behavior["quiet_cycle"]
    require(isinstance(quiet, int) and 0 <= quiet <= 16, f"Behaviour {name}: quiet_cycle 0..16")


def check_profile(profile, manifest):
    art = profile["art"]
    for field in ("scale", "icon_scale", "portrait_scale"):
        value = profile[field]
        require(isinstance(value, int) and 1 <= value <= 8, f"Profile {art}: {field} 1..8")
    frames = [a for a in manifest["assets"] if a["kind"] == "creatures" and a.get("form") == art]
    require(frames, f"Profile {art}: no creature frames")
    for frame in frames:
        left, top, right, bottom = frame["bounds"]
        width, height = (right - left) * profile["scale"], (bottom - top) * profile["scale"]
        require(width <= MAX_ACTOR_WIDTH and height <= FLOOR_Y - HEADING_BOTTOM,
                f"Profile {art}: {frame['key']} is {width}x{height} at scale {profile['scale']}")
        require(frame["width"] * profile["icon_scale"] <= 96, f"Profile {art}: icon exceeds a grid cell")


def load(manifest):
    forms = sorted(json.loads(CATALOG.read_text())["forms"], key=lambda f: f["id"])
    data = json.loads(CREATURES.read_text())
    require(data.get("version") == 1 and {"version", "behaviors", "profiles"} <= set(data) <=
            {"version", "behaviors", "profiles", "state_presentation"}, "Unknown creature data schema")
    behaviors = {}
    for behavior in data["behaviors"]:
        check_behavior(behavior, pose_names(manifest))
        require(behavior["name"] not in behaviors, f"Duplicate behaviour {behavior['name']}")
        behaviors[behavior["name"]] = behavior
    profiles = {}
    for profile in data["profiles"]:
        require(profile["art"] not in profiles, f"Duplicate profile {profile['art']}")
        require(profile["behavior"] in behaviors, f"Profile {profile['art']}: unknown behaviour")
        check_profile(profile, manifest)
        profiles[profile["art"]] = profile
    require(set(profiles) == {f["art"] for f in forms}, "Profiles must cover every catalog form")
    return forms, profiles, behaviors


def pose_names(manifest):
    """Base poses in enum order, then the manifest's state poses."""
    return list(CREATURE_POSES) + [p["name"] for p in manifest.get("state_poses", [])]


def load_looks(manifest):
    """One presentation per behaviour state, in content/behaviors.json state order."""
    states = [s["name"] for s in json.loads(BEHAVIORS.read_text())["states"]]
    looks = {p["state"]: p for p in json.loads(CREATURES.read_text()).get("state_presentation", [])}
    require(set(looks) == set(states), f"state_presentation must cover exactly: {states}")
    keys = {a["key"]: a for a in manifest["assets"]}
    for name, look in looks.items():
        require(look["pose"] in pose_names(manifest), f"State {name}: unknown pose {look['pose']}")
        caption = look.get("caption", "")
        require(len(caption) <= CAPTION_LIMIT and all(c.isupper() or c in " !?.,0123456789" for c in caption),
                f"State {name}: caption is upper case, at most {CAPTION_LIMIT}")
        for field in ("effect", "prop"):
            require(look.get(field) is None or look[field] in keys, f"State {name}: unknown {field} asset")
    return [looks[name] for name in states], keys


def clip_rows(forms, manifest):
    assets = {a["key"]: a for a in manifest["assets"]}
    clips = {c["key"]: c for c in manifest["clips"]}
    rows = []
    for form in forms:
        art = form["art"]
        portrait = next((a for a in assets.values() if a["id"] == form["portrait"]), None)
        require(portrait is not None and portrait.get("form") == art,
                f"Portrait {form['portrait']} is not a {art} frame")
        cells = []
        fallback = {p["name"]: p["fallback"] for p in manifest.get("state_poses", [])}
        for pose in pose_names(manifest):
            clip = clips.get(f"{art}.{pose}") or clips.get(f"{art}.{fallback.get(pose, pose)}")
            require(clip is not None, f"Form {form['name']} has no {art}.{pose} clip")
            frames = ", ".join(f"{assets[k]['id']}u" for k in clip["frames"])
            holds = ", ".join(f"{n}u" for n in clip["durations_ms"])
            loop = "true" if clip["loop"] else "false"
            cells.append(f"        {{{{{frames}}}, {{{holds}}}, {len(clip['frames'])}u, {loop}}}, /* {pose} */")
        rows.append(f"    {{ /* {form['name']} ({art}) */\n" + "\n".join(cells) + "\n    },")
    return rows


def profile_row(form, profile, behavior, poses):
    rules = ", ".join(f"{{{CREATURE_CONDITIONS.index(r['when'])}u, {poses.index(r['pose'])}u}}"
                      for r in behavior["pose_rules"])
    beats = ", ".join(f"{IDLE_POSES.index(b)}u" for b in behavior["idle_beats"])
    return (f"    {{{{{rules}}}, {len(behavior['pose_rules'])}u, {{{beats}}}, {len(behavior['idle_beats'])}u, "
            f"{behavior['quiet_cycle']}u, {profile['scale']}u, {profile['icon_scale']}u, "
            f"{profile['portrait_scale']}u}}, /* {form['name']}: {behavior['name']} */")


def emit(manifest):
    """C chunks for the generated asset translation unit."""
    forms, profiles, behaviors = load(manifest)
    rows = clip_rows(forms, manifest)
    count = len(forms)
    poses = pose_names(manifest)
    profile_rows = [profile_row(f, profiles[f["art"]], behaviors[profiles[f["art"]]["behavior"]], poses) for f in forms]
    looks, keys = load_looks(manifest)
    look_rows = [f"    {{{poses.index(l['pose'])}u, \"{l.get('caption', '')}\", "
                 f"{keys[l['effect']]['id'] if l.get('effect') else 0}u, "
                 f"{keys[l['prop']]['id'] if l.get('prop') else 0}u}}, /* {l['state']} */" for l in looks] or ["    {0}, /* no states */"]
    return ["#include \"jelli/creature.h\"",
            f"const unsigned jelli_creature_pose_count = {len(poses)}u;",
            f"static const JelliClip creature_clips[{count}][{len(poses)}] = {{\n" + "\n".join(rows) + "\n};",
            f"static const JelliBehaviorLook behavior_looks[] = {{\n" + "\n".join(look_rows) + "\n};",
            "const JelliBehaviorLook *jelli_behavior_look(unsigned state)\n{\n"
            f"    return state < {len(looks)}u ? &behavior_looks[state] : 0;\n}}\n",
            f"static const JelliCreatureProfile creature_profiles[{count}] = {{\n" + "\n".join(profile_rows) + "\n};",
            "const JelliClip *jelli_creature_clip(unsigned form, unsigned pose)\n{\n"
            f"    if (form >= {count}u || pose >= {len(poses)}u)\n        return 0;\n"
            "    return &creature_clips[form][pose];\n}\n",
            "const JelliCreatureProfile *jelli_creature_profile(unsigned form)\n{\n"
            f"    return &creature_profiles[form < {count}u ? form : 0u];\n}}\n"]
