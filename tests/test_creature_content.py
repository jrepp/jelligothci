"""Reject invalid creature behaviour and render profiles before they reach C."""
import copy
import json
import sys
from pathlib import Path
from unittest import mock

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools/assets"))
import creature_data  # noqa: E402
from build_slice import load_assets  # noqa: E402

manifest, _ = load_assets()
original = json.loads(creature_data.CREATURES.read_text())


def check(data, valid):
    with mock.patch.object(creature_data.Path, "read_text", autospec=True) as read:
        read.side_effect = lambda path: json.dumps(data) if path == creature_data.CREATURES \
            else Path.read_bytes(path).decode()
        try:
            creature_data.emit(manifest)
            ok = True
        except (ValueError, KeyError) as error:
            ok, message = False, error
    assert ok == valid, (data, None if ok else message)


check(original, True)
mutations = [
    lambda d: d["behaviors"][0]["pose_rules"].append({"when": "flying", "pose": "happy"}),
    lambda d: d["behaviors"][0]["pose_rules"].append({"when": "eating", "pose": "happy"}),
    lambda d: d["behaviors"][0]["pose_rules"][0].update(pose="dancing"),
    lambda d: d["behaviors"][0].update(pose_rules=[]),
    lambda d: d["behaviors"][0].update(idle_beats=["idle"] * 17),
    lambda d: d["behaviors"][0].update(idle_beats=["eating"]),
    lambda d: d["behaviors"][0].update(quiet_cycle=-1),
    lambda d: d["profiles"][0].update(scale=0),
    lambda d: d["profiles"][2].update(scale=8),  # 39x27 axolotl would not fit above the floor.
    lambda d: d["profiles"][2].update(icon_scale=3),  # 144 px icon in a 96 px grid cell.
    lambda d: d["profiles"][0].update(behavior="missing"),
    lambda d: d["profiles"].pop(),
    lambda d: d["profiles"].append(dict(d["profiles"][0])),
    lambda d: d.update(version=2),
    lambda d: d.update(extra=True),
]
for mutate in mutations:
    data = copy.deepcopy(original)
    mutate(data)
    check(data, False)
print("PASS: creature behaviours and render profiles reject invalid authoring")
