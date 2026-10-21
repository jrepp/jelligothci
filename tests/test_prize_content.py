"""Bounded append-only prize registry generation and artwork validation."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = json.loads((ROOT / "content/prizes.json").read_text())
ASSETS = json.loads((ROOT / "assets/slice/assets.json").read_text())
with tempfile.TemporaryDirectory(prefix="jelli-prizes-") as scratch:
    work = Path(scratch)
    (work / "cmake").mkdir()
    (work / "content").mkdir()
    (work / "assets/slice").mkdir(parents=True)
    shutil.copy(ROOT / "cmake/JelliPrizes.cmake", work / "cmake")
    driver = work / "check.cmake"
    driver.write_text('cmake_minimum_required(VERSION 3.21)\n'
                      'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliPrizes.cmake")\n'
                      'jelli_prizes_data(output)\n')

    def check(prizes, assets, valid):
        (work / "content/prizes.json").write_text(json.dumps(prizes))
        (work / "assets/slice/assets.json").write_text(json.dumps(assets))
        result = subprocess.run(["cmake", "-P", str(driver)], cwd=work,
                                capture_output=True, text=True, timeout=20, check=False)
        assert (result.returncode == 0) == valid, result.stderr
        return (work / "generated/jelli_prizes_data.c").read_text() if valid else ""

    check(ORIGINAL, ASSETS, True)
    prizes, assets = copy.deepcopy(ORIGINAL), copy.deepcopy(ASSETS)
    for i in range(len(ORIGINAL["prizes"]), 20):
        prizes["prizes"].append({"id": i, "key": f"test-{i}", "name": f"TEST {i}", "asset": 12000 + i})
        assets["assets"].append({"id": 12000 + i, "key": f"prizes.test-{i}", "kind": "prizes", "width": 32, "height": 32})
    generated = check(prizes, assets, True)
    assert '"test-19", "TEST 19", 12019u' in generated
    assert 'jelli_prize_count = 20u' in generated
    for change in ({"asset": 65000}, {"asset": 6001}, {"id": 20},
                   {"key": "butterfly"}, {"name": 'BAD"NAME'}, {"asset": 11001}, {"asset": "011001"}, {"asset": "12019"},
                   {"id": "19"}, {"id": "019"}, {"id": True}, {"asset": False},
                   {"id": 19.5}, {"asset": 12019.5}):
        invalid = copy.deepcopy(prizes)
        invalid["prizes"][-1].update(change)
        check(invalid, assets, False)
    overflow = copy.deepcopy(prizes)
    overflow["prizes"].append({"id": 20, "key": "overflow", "name": "OVERFLOW", "asset": 13000})
    check(overflow, assets, False)
    for field, value in (("kind", "items"), ("width", 64), ("height", 0), ("key", "prizes.wrong"),
                         ("id", "012019")):
        invalid_assets = copy.deepcopy(assets)
        invalid_assets["assets"][-1][field] = value
        check(prizes, invalid_assets, False)
    for slot in range(len(ORIGINAL["prizes"])):
        invalid = copy.deepcopy(ORIGINAL)
        invalid["prizes"][slot]["key"] = "replacement"
        check(invalid, ASSETS, False)
    invalid = copy.deepcopy(ORIGINAL)
    invalid["prizes"].pop()
    check(invalid, ASSETS, False)
print("PASS: appended prize capacity, stable saved identities, missing/wrong artwork rejection")
