"""Reject invalid behaviour authoring through the same CMake generator the build uses."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
behaviors = json.loads((root / "content/behaviors.json").read_text())
with tempfile.TemporaryDirectory(prefix="jelli-behaviors-") as directory:
    work = Path(directory)
    (work / "cmake").mkdir()
    (work / "content").mkdir()
    shutil.copy(root / "cmake/JelliBehaviors.cmake", work / "cmake")
    for name in ("activities.json", "pets.json"):
        shutil.copy(root / "content" / name, work / "content")
    driver = work / "check.cmake"
    # Standalone -P scripts do not inherit the project's CMake policy baseline (memo-035).
    driver.write_text('cmake_minimum_required(VERSION 3.21)\n'
                      'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliBehaviors.cmake")\n'
                      'jelli_behaviors_data(output)\n')

    def check(data, valid):
        (work / "content/behaviors.json").write_text(json.dumps(data))
        result = subprocess.run(["cmake", "-P", str(driver)], cwd=work,
                                capture_output=True, text=True, timeout=20)
        assert (result.returncode == 0) == valid, (result.stderr or "accepted invalid data")

    check(behaviors, True)
    reaction = lambda d: d["repertoires"][0]["reactions"][0]  # noqa: E731
    state = lambda d: d["states"][0]  # noqa: E731
    mutations = [
        lambda d: reaction(d).update(on="sneezing"),
        lambda d: reaction(d).update(state="flying"),
        lambda d: reaction(d).update(chance_pct=0),
        lambda d: reaction(d).update(weight=101),
        lambda d: reaction(d).update(when={"location": "moon"}),
        lambda d: reaction(d).update(on="activity_finished", moment="DANCING"),
        lambda d: reaction(d).update(on="need_low", need="sleepiness"),
        lambda d: state(d).update(duration_s=[10, 5]),
        lambda d: state(d).update(effects={"bond": 500}),
        lambda d: state(d).update(ends_on=["teleport"]),
        lambda d: state(d).update(request={"command": "juggle", "bond": 5}),
        lambda d: state(d).update(on_timeout="explode"),
        lambda d: state(d).update(name="Not Snake"),
        lambda d: d["states"].append(dict(state(d))),  # Duplicate state name.
        lambda d: d["repertoires"][0].update(forms=["UNICORN"]),
        lambda d: d["repertoires"].append(dict(d["repertoires"][0], name="twin")),  # Form twice.
        lambda d: d["repertoires"][0].update(affinities=[{"moment": "READING", "bonus_pct": 999}]),
        lambda d: d["touch"].update(overload_load=d["touch"]["upset_load"]),
        lambda d: d["touch"].update(reaction_ticks=0),
        lambda d: d["repertoires"][0].update(touch=dict(d["touch"], load_per_tap=0)),
        lambda d: d.update(need_low=0),
        lambda d: d["night"].update(start_minute=1440),
        lambda d: d["repertoires"][0].update(forms=[]),
        lambda d: d["repertoires"][0].update(reactions=[]),
    ]
    for mutate in mutations:
        data = copy.deepcopy(behaviors)
        mutate(data)
        check(data, False)
    gentle = copy.deepcopy(behaviors)  # A species may override touch as a whole block.
    gentle["repertoires"][0]["touch"] = dict(gentle["touch"], load_per_tap=120, bond_gain=6)
    check(gentle, True)
print("PASS: behaviour content rejects unknown names, out-of-range tuning and shared forms")
