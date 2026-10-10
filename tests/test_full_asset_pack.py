"""Export and load all authored art, exercising publisher and SDL bank limits."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/assets"))
import build_slice
import live_assets


with tempfile.TemporaryDirectory(prefix="jelli-full-pack-") as directory:
    output = Path(directory)
    manifest, images = build_slice.load_assets()
    build_slice.export_pixels(output, manifest, images)
    pack = output / "full.jlap"
    live_assets.publish_pack(ROOT / "assets/slice", pack)
    result = subprocess.run(
        [sys.argv[1], "--pet", "--headless", "--frames", "5", "--asset-pack", str(pack)],
        capture_output=True, text=True, timeout=15, check=True,
    )
    expected = f"Live artwork applied: {len(manifest['assets'])} assets"
    assert expected in result.stderr, result.stderr
    assert "unavailable or invalid" not in result.stderr, result.stderr
print("PASS: complete authored art exports and loads into SDL live banks")
