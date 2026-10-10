"""Creature behaviour and render profiles for Jelli Art: content/creatures.json.

Each catalog form (content/pets.json `art`) has a profile with its actor, icon
and portrait scales and a named behaviour: first-match-wins condition-to-pose
rules, an idle beat schedule and a quiet cycle. The served checkout's
tools/assets/creature_data.py decides what is valid; the studio only mirrors its
limits for the page and validates candidates through its load().
"""
import hashlib
import json
import tempfile
from pathlib import Path

# Fallbacks for checkouts whose creature_data lacks a constant; the module's values win.
DEFAULT_LIMITS = {
    "conditions": ["asleep", "wake_groggy", "wake_surprised", "wake_happy", "unwell",
                   "eating", "playing", "touch_happy", "touch_upset"],
    "idle_poses": ["idle", "idle-alt", "curious", "content"],
    "rule_capacity": 12, "beat_capacity": 16, "quiet_max": 16, "scale_min": 1, "scale_max": 8,
    "max_actor_width": 300, "max_actor_height": 176, "max_icon": 96, "beat_ms": 900, "floor_y": 256,
}


class ProfileError(ValueError):
    """A creature data edit the studio refuses, reported to the page as a 400."""


def digest(path):
    """Short content hash; the page sends it back so a save never overwrites unseen changes."""
    return hashlib.sha1(Path(path).read_bytes()).hexdigest()[:16] if Path(path).exists() else None


def limits(module):
    out = dict(DEFAULT_LIMITS)
    if module is None:
        return out
    if hasattr(module, "CREATURE_CONDITIONS"):
        out["conditions"] = list(module.CREATURE_CONDITIONS)
    if hasattr(module, "IDLE_POSES"):
        out["idle_poses"] = list(module.IDLE_POSES)
    for key, name in (("rule_capacity", "RULE_CAPACITY"), ("beat_capacity", "BEAT_CAPACITY"),
                      ("max_actor_width", "MAX_ACTOR_WIDTH"), ("floor_y", "FLOOR_Y")):
        out[key] = getattr(module, name, out[key])
    if hasattr(module, "FLOOR_Y") and hasattr(module, "HEADING_BOTTOM"):
        out["max_actor_height"] = module.FLOOR_Y - module.HEADING_BOTTOM
    return out


def profile_data(path, module):
    """What the page needs: the document, its hash and the validator's limits."""
    path = Path(path)
    data = None
    if path.exists():
        try:
            data = json.loads(path.read_text())
        except ValueError:
            data = None
    return {"creature_data": data, "creature_data_sha": digest(path),
            "creature_limits": limits(module), "creature_data_editable": module is not None and data is not None}


def validate(data, manifest, module, pets_path, behaviors=None):
    """Run creature_data.load(manifest), and load_looks(manifest) when the checkout has it, on scratch copies.

    behaviors is the content/behaviors.json document the looks must cover (the
    candidate when both files are saved together); None leaves the module's path.
    """
    if module is None or not hasattr(module, "load"):
        raise ProfileError("This checkout has no tools/assets/creature_data.py, so creature data cannot be validated")
    if not isinstance(data, dict):
        raise ProfileError("Creature data must be a JSON object")
    with tempfile.TemporaryDirectory(prefix="jelli-art-creatures-") as scratch:
        candidate = Path(scratch) / "creatures.json"
        candidate.write_text(json.dumps(data, indent=2) + "\n")
        globals_ = ("CREATURES", "CATALOG", "BEHAVIORS")
        saved = {name: getattr(module, name) for name in globals_ if hasattr(module, name)}
        module.CREATURES, module.CATALOG = candidate, Path(pets_path)
        if behaviors is not None and "BEHAVIORS" in saved:
            module.BEHAVIORS = Path(scratch) / "behaviors.json"
            module.BEHAVIORS.write_text(json.dumps(behaviors, indent=2) + "\n")
        try:
            module.load(manifest)
            if hasattr(module, "load_looks") and hasattr(module, "BEHAVIORS") and Path(module.BEHAVIORS).exists():
                module.load_looks(manifest)
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            detail = str(error) if isinstance(error, ValueError) else f"missing or malformed field {error}"
            raise ProfileError(f"Creature data fails validation: {detail}") from error
        finally:
            for name, value in saved.items():
                setattr(module, name, value)
