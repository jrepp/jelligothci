"""Reject invalid authored catalogs without needing SDL, Python packages, or firmware."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / "content/pets.json").read_text())
with tempfile.TemporaryDirectory(prefix="jelli-content-") as directory:
    work = Path(directory)
    (work / "cmake").mkdir()
    (work / "content").mkdir()
    shutil.copy(root / "cmake/JelliCollection.cmake", work / "cmake")
    driver = work / "check.cmake"
    driver.write_text('include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliCollection.cmake")\n'
                      'jelli_collection_data(output)\n')

    def check(data, valid):
        (work / "content/pets.json").write_text(json.dumps(data))
        result = subprocess.run(["cmake", "-P", str(driver)], cwd=work,
                                capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == valid, result.stderr

    check(catalog, True)
    for field, value in (("id", 2), ("unlock_prize", 10), ("evolution_set", 99),
                         ("name", 'bad"name'), ("hint", "X" * 25)):
        invalid = copy.deepcopy(catalog)
        invalid["entries"][0][field] = value
        check(invalid, False)
    for count in (8, 10):
        invalid = copy.deepcopy(catalog)
        invalid["entries"] = (invalid["entries"] + [invalid["entries"][0]])[:count]
        check(invalid, False)
    invalid = copy.deepcopy(catalog)
    invalid["evolution_sets"][0]["forms"] = [1, 0]
    check(invalid, False)
    invalid["evolution_sets"][0]["forms"] = [0, 0]
    check(invalid, False)
    invalid = copy.deepcopy(catalog)
    invalid["evolution_sets"][0]["growth_ticks"] = 0
    check(invalid, False)
print("PASS: bounded catalog and evolution references reject invalid authoring")
