"""Behaviour content for Jelli Art: content/behaviors.json and its vocabulary (RFC-005).

The engine owns the vocabulary. Stimulus, need, command, health, care and
location names come from the set() lists at the top of the checkout's
cmake/JelliBehaviors.cmake, and the numbers the simulator needs (stimulus kinds,
activity codes, need and command enums) come from the checkout's C headers, so
the studio follows the engine instead of copying it. cmake/JelliBehaviors.cmake
is also the validator: a candidate is checked by running it with `cmake -P` on a
scratch copy, as tests/test_collection_content.py does for pets.json.
"""
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ANY = 255  # JELLI_BEHAVIOR_ANY
# Bounds enforced by cmake/JelliBehaviors.cmake; mirrored only to guide the page.
LIMITS = {"states": 16, "repertoires": 8, "reactions_total": 64, "affinities_total": 16,
          "duration_max_s": 3600, "cooldown_max_s": 36000, "effect": 100, "request_bond": 100,
          "weight_max": 100, "chance_max": 100, "mood_max": 100, "bond_max": 1000, "bonus_max": 200,
          "prize_max": 8, "minute_max": 1439, "need_low_max": 999, "timeouts": ["nothing", "accident"]}
HEADERS = ("include/jelli/game.h", "include/jelli/behavior.h")


class BehaviorError(ValueError):
    """A behaviour edit the studio refuses, reported to the page as a 400."""


def cmake_lists(path):
    """{JELLI_BEHAVIOR_STIMULI: [...], ...} from the set() calls in a CMake file."""
    text = Path(path).read_text()
    return {name: values.split() for name, values in re.findall(r"set\((JELLI_BEHAVIOR_\w+)\s+([^)]*)\)", text)}


def c_enums(paths):
    """Enumerator values from C headers: implicit counting and integer initialisers only."""
    values = {}
    for path in paths:
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", Path(path).read_text(), flags=re.S)
        for body in re.findall(r"\benum\s*\w*\s*\{([^}]*)\}", text):
            nxt = 0
            for entry in filter(None, (e.strip() for e in body.split(","))):
                name, _, init = (part.strip() for part in entry.partition("="))
                if init:
                    try:
                        nxt = int(init, 0)
                    except ValueError:
                        continue  # an expression; the vocabulary below never needs one
                values[name] = nxt
                nxt += 1
    return values


def read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None


def vocabulary(repo, content):
    """Names and numbers the Behaviour view and simulator use, or None when the checkout has no engine."""
    cmake = Path(repo) / "cmake/JelliBehaviors.cmake"
    if not cmake.exists():
        return None
    lists = cmake_lists(cmake)
    short = {k.removeprefix("JELLI_BEHAVIOR_").lower(): v for k, v in lists.items()}
    enums = c_enums([Path(repo) / h for h in HEADERS if (Path(repo) / h).exists()])
    activities = read_json(Path(content) / "activities.json") or {}
    pets = read_json(Path(content) / "pets.json") or {}
    up = str.upper
    numbers = {
        "stimuli": {s: enums.get(f"JELLI_STIM_{up(s)}") for s in short.get("stimuli", [])},
        "needs": {n: enums.get(f"JELLI_{up(n)}") for n in short.get("needs", [])},
        "health": {h: enums.get(f"JELLI_HEALTH_{up(h)}") for h in short.get("health", [])},
        "care": {c: enums.get(f"JELLI_{up(c)}") for c in short.get("care", [])},
        "commands": {c: enums.get(f"JELLI_CMD_{up(c)}") for c in short.get("commands", [])},
        "activity_code": {k: enums.get(f"JELLI_ACTIVITY_CODE_{up(k)}") for k in ("moment", "health", "care")},
    }
    missing = [f"{group}.{name}" for group, table in numbers.items() for name, v in table.items() if v is None]
    return {"lists": short, "numbers": numbers, "missing_numbers": missing, "any": ANY, "limits": LIMITS,
            "moments": [m.get("name") for m in activities.get("moments", [])],
            "forms": [{"name": f.get("name"), "art": f.get("art"), "id": f.get("id")} for f in pets.get("forms", [])]}


def cmake_messages(stderr):
    """The message() text of CMake errors, without call stacks."""
    out, take = [], False
    for line in stderr.splitlines():
        if line.startswith("CMake Error"):
            take = True
            continue
        if take and (not line.strip() or line.strip().startswith("Call Stack")):
            take = False
            continue
        if take:
            out.append(line.strip())
    return " ".join(out) or stderr.strip()[-400:]


def validate(doc, repo, content):
    """Run cmake/JelliBehaviors.cmake on a scratch copy holding this document."""
    cmake = shutil.which("cmake")
    source = Path(repo) / "cmake/JelliBehaviors.cmake"
    if not cmake:
        raise BehaviorError("cmake is not installed here, so behaviour edits cannot be validated")
    if not source.exists():
        raise BehaviorError("This checkout has no cmake/JelliBehaviors.cmake")
    if not isinstance(doc, dict):
        raise BehaviorError("Behaviour data must be a JSON object")
    with tempfile.TemporaryDirectory(prefix="jelli-art-behaviors-") as scratch:
        work = Path(scratch)
        (work / "cmake").mkdir()
        (work / "content").mkdir()
        shutil.copy(source, work / "cmake")
        for name in ("activities.json", "pets.json"):
            shutil.copy(Path(content) / name, work / "content")
        (work / "content/behaviors.json").write_text(json.dumps(doc, indent=2) + "\n")
        driver = work / "check.cmake"
        # Standalone -P scripts do not inherit the project's CMake policy baseline (memo-035).
        driver.write_text('cmake_minimum_required(VERSION 3.21)\n'
                          'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliBehaviors.cmake")\njelli_behaviors_data(output)\n')
        result = subprocess.run([cmake, "-P", str(driver)], cwd=work, capture_output=True, text=True, timeout=20, check=False)
    if result.returncode:
        raise BehaviorError(f"Behaviour data fails validation: {cmake_messages(result.stderr)}")
