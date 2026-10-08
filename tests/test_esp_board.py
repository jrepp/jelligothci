"""ESP32 board selection checks that need no SDK, toolchain, or serial port.

scripts/esp runs against a fake SDK checkout whose activation only puts a
recording idf.py on PATH, so the test sees the exact idf.py arguments.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
ESP_PORT = REPOSITORY / "ports/esp32"
BOARDS = {"1.75": "build/esp32", "1.75c": "build/esp32-1.75c"}
INVALID = ("1.76", "../x", "1.75 c", "1.75c;rm", "1.75C", " 1.75", "1.75\n", "-1.75", "1.75;1.75c")


def lock_version(lock_text, name):
    """Return the resolved version of a top-level component in a lock file."""
    block = re.search(rf"^  {re.escape(name)}:\n((?:    .*\n|      .*\n)*)", lock_text, re.M)
    if block is None:
        return None
    version = re.search(r"^    version: '?([^'\n]+)'?$", block.group(1), re.M)
    return version.group(1) if version else None


class EspScriptBoardTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="jelli-esp-board-"))
        self.addCleanup(shutil.rmtree, self.root)
        self.repo = self.root / "repo"
        (self.repo / "scripts").mkdir(parents=True)
        shutil.copy2(REPOSITORY / "scripts/esp", self.repo / "scripts/esp")
        sdk = self.repo / ".tools/esp-idf"
        (sdk / "tools").mkdir(parents=True)
        fake_bin = self.root / "bin"
        fake_bin.mkdir()
        self.idf_log = self.root / "idf-args"
        self.activated = self.root / "activated"
        (sdk / "tools/activate.py").write_text(
            "from pathlib import Path\n"
            f"Path({str(self.activated)!r}).touch()\n"
            f"print('export PATH={fake_bin}:\"$PATH\"')\n"
        )
        recorder = fake_bin / "idf.py"
        recorder.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$@\" > '{self.idf_log}'\n")
        recorder.chmod(0o755)
        # scripts/esp accepts only Python 3.10-3.13; the shim passes that probe so
        # the test does not depend on which interpreter runs it.
        self.python = fake_bin / "python-shim"
        self.python.write_text(f'#!/bin/sh\n[ "$1" = -c ] && exit 0\nexec "{sys.executable}" "$@"\n')
        self.python.chmod(0o755)
        git = ["git", "-C", str(sdk), "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]
        subprocess.run(git[:3] + ["init", "-q"], check=True)
        subprocess.run(git + ["add", "."], check=True)
        subprocess.run(git + ["commit", "-q", "-m", "fake sdk"], check=True)
        commit = subprocess.run(git[:3] + ["rev-parse", "HEAD"], check=True, capture_output=True, text=True)
        (self.repo / "toolchain.env").write_text(
            f"JELLI_IDF_VERSION=v0.0.0\nJELLI_IDF_COMMIT={commit.stdout.strip()}\n"
        )

    def run_esp(self, *arguments, board=None):
        environment = {k: v for k, v in os.environ.items() if k != "JELLI_ESP_BOARD"}
        environment["JELLI_PYTHON"] = str(self.python)
        if board is not None:
            environment["JELLI_ESP_BOARD"] = board
        return subprocess.run(
            [str(self.repo / "scripts/esp"), *arguments],
            env=environment, capture_output=True, text=True, timeout=60,
        )

    def idf_arguments(self):
        return self.idf_log.read_text().splitlines()

    def assert_board(self, result, board):
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.idf_arguments()
        build_dir = arguments[arguments.index("-B") + 1]
        self.assertEqual(Path(build_dir).resolve(), self.repo.resolve() / BOARDS[board])
        self.assertIn(f"-DJELLI_ESP_BOARD={board}", arguments)
        self.assertIn("-DIDF_TARGET=esp32s3", arguments)
        self.assertEqual(Path(arguments[arguments.index("-C") + 1]).resolve(), self.repo.resolve() / "ports/esp32")

    def test_unset_board_builds_the_original_board_in_the_original_directory(self):
        result = self.run_esp("build")
        self.assert_board(result, "1.75")
        self.assertEqual(self.idf_arguments()[-1], "build")

    def test_empty_board_falls_back_to_the_default(self):
        self.assert_board(self.run_esp("build", board=""), "1.75")

    def test_each_board_uses_its_own_build_directory(self):
        for board in BOARDS:
            with self.subTest(board=board):
                self.idf_log.unlink(missing_ok=True)
                self.assert_board(self.run_esp("build", board=board), board)

    def test_idf_passthrough_keeps_board_and_caller_arguments(self):
        result = self.run_esp("idf", "size-components", "--format", "json", board="1.75c")
        self.assert_board(result, "1.75c")
        self.assertEqual(self.idf_arguments()[-3:], ["size-components", "--format", "json"])

    def test_invalid_boards_fail_before_activation_or_idf(self):
        for board in INVALID:
            for command in ("build", "idf", "doctor"):
                with self.subTest(board=board, command=command):
                    result = self.run_esp(command, board=board)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(f"Unknown JELLI_ESP_BOARD '{board}'", result.stderr)
                    self.assertIn("expected 1.75 or 1.75c", result.stderr)
                    self.assertFalse(self.activated.exists(), "SDK was activated")
                    self.assertFalse(self.idf_log.exists(), "idf.py was invoked")

    def test_help_names_the_board_variable(self):
        result = self.run_esp("help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("JELLI_ESP_BOARD=1.75c", result.stdout)
        self.assertFalse(self.activated.exists())


class EspCMakeBoardTests(unittest.TestCase):
    """Configure the ESP project without an SDK; validation runs before IDF loads."""

    def configure(self, board):
        with tempfile.TemporaryDirectory(prefix="jelli-esp-cmake-") as directory:
            environment = dict(os.environ, IDF_PATH=str(Path(directory) / "missing-sdk"))
            return subprocess.run(
                [os.environ.get("JELLI_CMAKE", "cmake"), "-S", str(ESP_PORT), "-B", str(Path(directory) / "build"), f"-DJELLI_ESP_BOARD={board}"],
                env=environment, capture_output=True, text=True, timeout=60,
            )

    def test_unknown_board_stops_configuration(self):
        for board in INVALID + ("",):
            with self.subTest(board=board):
                result = self.configure(board)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Unknown JELLI_ESP_BOARD", result.stderr)
                self.assertNotIn("project.cmake", result.stderr, "validation must stop before the SDK loads")

    def test_known_boards_pass_validation_and_reach_the_sdk(self):
        for board in BOARDS:
            with self.subTest(board=board):
                result = self.configure(board)
                self.assertNotIn("Unknown JELLI_ESP_BOARD", result.stderr)
                self.assertIn("project.cmake", result.stderr)


class EspBoardLayoutTests(unittest.TestCase):
    """The board list, wrapper components, BSP pins, and lock files must agree."""

    def test_cmake_script_and_directories_list_the_same_boards(self):
        cmake = (ESP_PORT / "CMakeLists.txt").read_text()
        listed = re.search(r"set\(jelli_esp_boards ([^)]*)\)", cmake).group(1)
        self.assertEqual(set(re.findall(r'"([^"]+)"', listed)), set(BOARDS))
        self.assertEqual({path.name for path in (ESP_PORT / "boards").iterdir() if path.is_dir()}, set(BOARDS))
        self.assertIn('set(JELLI_ESP_BOARD "1.75" CACHE', cmake)
        script = (REPOSITORY / "scripts/esp").read_text()
        for board, build_dir in BOARDS.items():
            self.assertIn(f'{board}) build_dir="$repo_dir/{build_dir}"', script)

    def test_each_board_has_a_distinct_label_and_an_exact_bsp_pin_matching_its_lock(self):
        labels = set()
        lvgl_pin = re.search(r'lvgl/lvgl: "==([^"]+)"', (ESP_PORT / "main/idf_component.yml").read_text()).group(1)
        for board in BOARDS:
            with self.subTest(board=board):
                component = ESP_PORT / "boards" / board / "jelli_board"
                label = re.search(r'JELLI_BOARD_LABEL="([^"]+)"', (component / "CMakeLists.txt").read_text())
                labels.add(label.group(1))
                manifest = (component / "idf_component.yml").read_text()
                bsp, version = re.search(r"^  (waveshare/[^:]+):\n    version: \"==([^\"]+)\"", manifest, re.M).groups()
                self.assertIn("public: true", manifest)
                lock_name = "dependencies.lock" if board == "1.75" else f"dependencies.{board}.lock"
                lock = (ESP_PORT / lock_name).read_text()
                self.assertEqual(lock_version(lock, bsp), version)
                self.assertEqual(lock_version(lock, "lvgl/lvgl"), lvgl_pin)
                direct = re.search(r"^direct_dependencies:\n((?:- .*\n)*)", lock, re.M).group(1)
                self.assertIn(f"- {bsp}\n", direct)
                self.assertEqual(sum(line.startswith("- waveshare/esp32_s3") for line in direct.splitlines()), 1)
        self.assertEqual(len(labels), len(BOARDS))


if __name__ == "__main__":
    unittest.main()
