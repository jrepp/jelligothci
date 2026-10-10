# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Loki Agent's own art (kind "agent", RFC-003) against the shipped assets/slice: the
agent's firmware embeds exactly the unicorn and the font, the pet's embed, live pack
and budget leave the agent's frames out, the frames are in the order the firmware
indexes them (include/jelli/agent_prance.h), and extract_prance.py reproduces the
committed PNGs byte for byte."""
import contextlib
import importlib
import io
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PETS = ROOT / "assets/slice/source/hires/pets"
sys.path.insert(0, str(ROOT / "tools/assets"))
sys.path.insert(0, str(PETS))
import build_slice  # noqa: E402
import embed_slice  # noqa: E402
import live_assets  # noqa: E402

SOURCE = ROOT / "assets/slice"
AGENT_KEYS = ("creatures.unicorn-", "agent.unicorn-")  # ports/esp32-agent/main/CMakeLists.txt
FONT = 4001
PRANCE_IDS = list(range(13001, 13009))
UNICORN_IDS = list(range(1041, 1049))


def embedded_ids(text):
    """The ids in the generated assets[] table, in order."""
    table = text[text.index("static const JelliAsset assets[] = {"):]
    return [int(m) for m in re.findall(r"^    \{(\d+)u,", table, re.M)]


def frame_order():
    """JelliPranceFrame's names, before JELLI_PRANCE_FRAMES, as asset key suffixes."""
    header = (ROOT / "include/jelli/agent_prance.h").read_text()
    body = re.search(r"typedef enum \{(.*?)\} JelliPranceFrame;", header, re.S).group(1)
    names = re.findall(r"JELLI_PRANCE_(\w+)", body)
    assert names[-1] == "FRAMES", names
    suffix = {"A": "prance-a", "B": "prance-b"}
    return [suffix.get(n, n.lower().replace("_", "-")) for n in names[:-1]]


class AgentArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.images = build_slice.load_assets(SOURCE)
        cls.agent = [a for a in cls.manifest["assets"] if a["kind"] in build_slice.AGENT_ONLY]
        cls.pet_ids = sorted(a["id"] for a in cls.manifest["assets"]
                             if a["kind"] not in build_slice.AGENT_ONLY)

    def test_agent_frames_are_in_the_order_the_firmware_indexes_them(self):
        # ui_art.c finds frame i at 13001 + i; prance.c names frames by JelliPranceFrame.
        by_id = sorted(self.agent, key=lambda a: a["id"])
        self.assertEqual([a["id"] for a in by_id], PRANCE_IDS)
        self.assertEqual([a["key"] for a in by_id], [f"agent.unicorn-{p}" for p in frame_order()])
        self.assertEqual(build_slice.AGENT_ONLY, {"agent"})
        for asset in by_id:
            with self.subTest(key=asset["key"]):
                self.assertEqual(asset["path"], f"hires/agent/{asset['key'].removeprefix('agent.')}.png")
                # ui_art.c refuses frames whose width or density differ from the first.
                self.assertEqual((asset["width"], asset["height"], asset["density"]), (32, 32, 6))
                with Image.open(SOURCE / asset["path"]) as image:
                    self.assertEqual((image.format, image.mode, image.size), ("PNG", "RGBA", (192, 192)))

    def test_the_agent_embed_is_the_sixteen_unicorn_frames_and_the_font(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "agent_assets.c"
            with contextlib.redirect_stdout(io.StringIO()):
                embed_slice.generate(output, SOURCE, AGENT_KEYS)
            text = output.read_text()
        self.assertEqual(embedded_ids(text), sorted([*UNICORN_IDS, FONT, *PRANCE_IDS]))
        for ident in [*UNICORN_IDS, *PRANCE_IDS]:  # High resolution: pixels and alpha, no mask.
            self.assertIn(f"static const uint8_t asset_{ident}_alpha[36864] = {{", text)
            self.assertNotIn(f"asset_{ident}_mask", text)

    def test_the_pet_embed_has_none_of_the_agent_art(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "jelli_assets.c"
            with contextlib.redirect_stdout(io.StringIO()):
                embed_slice.generate(output, SOURCE, ())
            text = output.read_text()
        self.assertEqual(embedded_ids(text), self.pet_ids)
        self.assertIsNone(re.search(r"asset_13\d{3}_", text))
        # Asking for the agent's keys by prefix is the only way in.
        self.assertFalse(any(embed_slice.selected(a, ()) for a in self.agent))
        self.assertTrue(all(embed_slice.selected(a, ("agent.",)) for a in self.agent))

    def test_the_live_pack_has_none_of_the_agent_art(self):
        data = live_assets.build_pack(SOURCE)
        _, _, count, _ = live_assets.HEADER.unpack_from(data)
        ids, at = [], live_assets.HEADER.size
        while at < len(data):
            record = live_assets.RECORD.unpack_from(data, at)
            ids.append(record[0])
            at += live_assets.RECORD.size + record[11]
        self.assertEqual(at, len(data))
        self.assertEqual((count, sorted(ids)), (len(self.pet_ids), self.pet_ids))

    def test_the_pet_budget_leaves_the_agent_art_out(self):
        with tempfile.TemporaryDirectory() as folder:
            report = build_slice.export_pixels(Path(folder), self.manifest, self.images)
        frame = 192 * 192 * 3  # RGB565 + 8-bit alpha per stored pixel.
        self.assertEqual(report["agent_pixel_bytes"], len(PRANCE_IDS) * frame)
        everything = sum(r["bytes"] for r in report["assets"])
        self.assertEqual(report["pixel_bytes"], everything - report["agent_pixel_bytes"])
        self.assertEqual(report["hires_assets"],
                         sum(1 for r in report["assets"] if r["density"] > 1) - len(PRANCE_IDS))


class ExtractPranceTest(unittest.TestCase):
    """The committed frames are what the extraction makes, and --check notices when not."""

    @classmethod
    def setUpClass(cls):
        cls.extract = importlib.import_module("extract_prance")

    def run_main(self, out_dir, *arguments):
        stdout, stderr = io.StringIO(), io.StringIO()
        with unittest.mock.patch.object(self.extract, "OUT_DIR", out_dir), \
                unittest.mock.patch.object(sys, "argv", ["extract_prance.py", *arguments]), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            return self.extract.main(), stderr.getvalue()

    def test_check_passes_on_the_committed_frames(self):
        script = PETS / "extract_prance.py"
        result = subprocess.run([sys.executable, str(script), "--check"], capture_output=True,
                                text=True, timeout=300, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("unicorn prance: scale", result.stdout)

    def test_extraction_is_deterministic_and_matches_the_files(self):
        first, scale = self.extract.extract()
        second, again = self.extract.extract()
        self.assertEqual(scale, again)
        self.assertEqual(list(first), list(self.extract.POSES))
        for pose in self.extract.POSES:
            data = self.extract.png_bytes(first[pose])
            self.assertEqual(data, self.extract.png_bytes(second[pose]), pose)
            self.assertEqual(data, (SOURCE / f"hires/agent/unicorn-{pose}.png").read_bytes(), pose)

    def test_check_fails_on_a_stale_or_missing_frame_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            for path in (SOURCE / "hires/agent").glob("*.png"):
                (out / path.name).write_bytes(path.read_bytes())
            self.assertEqual(self.run_main(out, "--check")[0], 0)
            stale = out / "unicorn-leap.png"
            with Image.open(stale) as image:
                image = image.copy()
            image.putpixel((0, 0), (1, 2, 3, 4))
            image.save(stale)
            (out / "unicorn-rear.png").unlink()
            before = {p.name: p.read_bytes() for p in out.iterdir()}
            code, errors = self.run_main(out, "--check")
            self.assertEqual(code, 1)
            self.assertIn("unicorn-leap.png", errors)
            self.assertIn("unicorn-rear.png", errors)
            self.assertNotIn("unicorn-stand.png", errors)
            self.assertEqual({p.name: p.read_bytes() for p in out.iterdir()}, before)
            # Without --check it rewrites them, and then --check passes again.
            self.assertEqual(self.run_main(out)[0], 0)
            self.assertEqual(self.run_main(out, "--check")[0], 0)


if __name__ == "__main__":
    unittest.main()
