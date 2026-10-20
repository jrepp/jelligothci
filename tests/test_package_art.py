"""Release packaging validates active prize artwork, not reserved slots."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_art", ROOT / "scripts/package_art.py")
package_art = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_art)

with tempfile.TemporaryDirectory(prefix="jelli-package-art-") as scratch:
    root = Path(scratch)
    (root / "content").mkdir()
    (root / "assets/slice").mkdir(parents=True)
    catalog = json.loads((ROOT / "content/prizes.json").read_text())
    manifest = json.loads((ROOT / "assets/slice/assets.json").read_text())
    package_art.ROOT = root
    (root / "content/prizes.json").write_text(json.dumps(catalog))

    def verify(changed_manifest, duplicate_registered=False, corrupt_extra=False):
        assets_dir = root / "assets/slice"
        shutil.rmtree(assets_dir)
        assets_dir.mkdir(parents=True)
        with zipfile.ZipFile(root / "release.zip", "w") as archive:
            for index, item in enumerate(changed_manifest["assets"]):
                relative = "assets/slice/" + item["path"]
                asset_path = assets_dir / item["path"]
                asset_path.parent.mkdir(parents=True, exist_ok=True)
                data = f"asset-{index}".encode()
                if duplicate_registered and item["id"] == catalog["prizes"][1]["asset"]:
                    first_id = catalog["prizes"][0]["asset"]
                    first_index = next(i for i, asset in enumerate(changed_manifest["assets"])
                                       if asset["id"] == first_id)
                    data = f"asset-{first_index}".encode()
                asset_path.write_bytes(data)
                packaged = b"corrupt" if corrupt_extra and item["id"] == 99999 else data
                archive.writestr(relative, packaged)
            archive.writestr("assets/slice/assets.json", json.dumps(changed_manifest).encode())
            for name in ("preview.html", "tools/assets/live_assets.py", "scripts/jelli-art-live", "ARTWORK.txt"):
                archive.writestr(name, b"present")
        (root / "assets/slice/assets.json").write_text(json.dumps(changed_manifest))
        try:
            package_art.verify_art(root / "release.zip")
            return True
        except RuntimeError:
            return False

    assert verify(manifest), "reserved capacity must not be required as packaged prize art"
    extra = copy.deepcopy(manifest)
    extra["assets"].append({"id": 99999, "key": "prizes.future", "path": "prizes/future.png",
                            "kind": "prizes", "width": 1, "height": 1})
    assert verify(extra), "unregistered prize art may be packaged without becoming an active prize"
    assert not verify(extra, corrupt_extra=True), "unregistered package bytes must still match the manifest"
    broken = copy.deepcopy(manifest)
    broken["assets"] = [item for item in broken["assets"] if item["id"] != catalog["prizes"][0]["asset"]]
    assert not verify(broken), "every catalog prize must have packaged artwork"
    assert not verify(manifest, duplicate_registered=True), "registered prize art must be distinct"
    ambiguous = copy.deepcopy(manifest)
    registered_item = next(item for item in ambiguous["assets"]
                           if item["id"] == catalog["prizes"][0]["asset"])
    duplicate_id = copy.deepcopy(registered_item)
    duplicate_id["path"] = "prizes/duplicate.png"
    ambiguous["assets"].append(duplicate_id)
    assert not verify(ambiguous), "duplicate prize asset IDs must be rejected"

print("PASS: package checks require distinct artwork for catalog prizes, not reserved capacity")
