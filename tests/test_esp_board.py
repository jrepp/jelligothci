"""ESP32 board selection checks that need no SDK, toolchain, or serial port.

scripts/esp runs against a fake SDK checkout whose activation only puts a
recording idf.py on PATH, so the test sees the exact idf.py arguments.
"""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
ESP_PORT = REPOSITORY / "ports/esp32"
AGENT_PORT = REPOSITORY / "ports/esp32-agent"
BOARDS = {"1.75": "build/esp32", "1.75c": "build/esp32-1.75c"}
AGENT_BOARDS = {"1.75": "build/esp32-agent", "1.75c": "build/esp32-agent-1.75c"}
INVALID_APPS = ("Agent", "pets", "../agent", "agent;rm", " agent", "agent\n")


def START_APP(port):
    """The single reset that ends flash-agent and boot and starts the selected app."""
    return ["-m", "esptool", "--port", port, "--after", "hard_reset", "chip_id"]
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
        # SDK tools (parttool, otatool) run through the activated "python"; record them.
        self.python_log = self.root / "python-args"
        tool = fake_bin / "python"
        tool.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$@\" >> '{self.python_log}'\n")
        tool.chmod(0o755)
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

    def run_esp(self, *arguments, board=None, app=None):
        environment = {k: v for k, v in os.environ.items() if k not in ("JELLI_ESP_BOARD", "JELLI_ESP_APP")}
        environment["JELLI_PYTHON"] = str(self.python)
        if board is not None:
            environment["JELLI_ESP_BOARD"] = board
        if app is not None:
            environment["JELLI_ESP_APP"] = app
        return subprocess.run(
            [str(self.repo / "scripts/esp"), *arguments],
            env=environment, capture_output=True, text=True, timeout=60,
        )

    def idf_arguments(self):
        return self.idf_log.read_text().splitlines()

    def assert_board(self, result, board, app="pet"):
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.idf_arguments()
        build_dir = arguments[arguments.index("-B") + 1]
        expected = (AGENT_BOARDS if app == "agent" else BOARDS)[board]
        self.assertEqual(Path(build_dir).resolve(), self.repo.resolve() / expected)
        self.assertIn(f"-DJELLI_ESP_BOARD={board}", arguments)
        self.assertIn("-DIDF_TARGET=esp32s3", arguments)
        project = "ports/esp32-agent" if app == "agent" else "ports/esp32"
        self.assertEqual(Path(arguments[arguments.index("-C") + 1]).resolve(), self.repo.resolve() / project)

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

    def test_each_app_and_board_has_its_own_project_and_build_directory(self):
        for app in ("pet", "agent"):
            for board in BOARDS:
                with self.subTest(app=app, board=board):
                    self.idf_log.unlink(missing_ok=True)
                    self.assert_board(self.run_esp("build", board=board, app=app), board, app)

    def test_unset_or_empty_app_builds_the_pet(self):
        self.assert_board(self.run_esp("build", app=""), "1.75")

    def test_invalid_apps_fail_before_activation_or_idf(self):
        for app in INVALID_APPS:
            for command in ("build", "flash-agent", "boot"):
                with self.subTest(app=app, command=command):
                    result = self.run_esp(command, "/dev/null", app=app)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(f"Unknown JELLI_ESP_APP '{app}'", result.stderr)
                    self.assertFalse(self.activated.exists(), "SDK was activated")

    def assert_refused(self, result, message):
        self.assertEqual(result.returncode, 2)
        self.assertIn(message, result.stderr)
        self.assertFalse(self.activated.exists(), "SDK was activated")
        self.assertFalse(self.idf_log.exists() or self.python_log.exists(), "a flash tool ran")

    def test_full_flash_of_the_agent_is_refused(self):
        # idf.py flash would write the bootloader, reset otadata, and put the agent in factory.
        self.assert_refused(self.run_esp("flash", "/dev/null", app="agent"), "scripts/esp flash-agent PORT")

    def test_flash_agent_requires_the_agent_app_and_one_port(self):
        self.assert_refused(self.run_esp("flash-agent", "/dev/null"), "JELLI_ESP_APP=agent")
        self.assert_refused(self.run_esp("flash-agent", app="agent"), "JELLI_ESP_APP=agent")
        self.assert_refused(self.run_esp("flash-agent", "/dev/a", "/dev/b", app="agent"), "JELLI_ESP_APP=agent")

    def test_flash_agent_builds_resets_the_boot_choice_then_writes_only_ota_0(self):
        # Erasing otadata first makes the pet start, so the new agent is only entered
        # through the pet's "app agent" and its trial boot (ADR-013).
        result = self.run_esp("flash-agent", "/dev/cu.test", board="1.75c", app="agent")
        self.assert_board(result, "1.75c", "agent")
        self.assertEqual(self.idf_arguments()[-1], "build")
        calls = self.python_log.read_text().splitlines()
        self.assertEqual(Path(calls[0]).name, "otatool.py")
        self.assertEqual(Path(calls[5]).name, "parttool.py")
        self.assertEqual(calls[1:5] + calls[6:], [
            "--port", "/dev/cu.test", "--esptool-args=after=no_reset", "erase_otadata",
            "--port", "/dev/cu.test", "--esptool-args=after=no_reset", "write_partition",
            "--partition-name", "ota_0", "--input", str(self.repo / "build/esp32-agent-1.75c/loki_agent.bin"),
            *START_APP("/dev/cu.test")])
        # No debug client in this checkout, so the pet is left to start the agent.
        self.assertIn("starts it at the next reset", result.stdout)

    def fake_debug(self, answers=True):
        """A scripts/jelli-debug that logs its arguments and answers (or not)."""
        log = self.root / "debug-args"
        debug = self.repo / "scripts/jelli-debug"
        debug.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$@\" >> '{log}'\nexit {0 if answers else 1}\n")
        debug.chmod(0o755)
        return log

    def test_flash_agent_hands_over_through_the_pets_app_agent(self):
        # ADR-015: the pet starts, then "app agent" enters the new agent on trial.
        log = self.fake_debug()
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(log.read_text().splitlines(), [
            "--port", "/dev/cu.test", "--timeout", "2", "state",
            "--port", "/dev/cu.test", "--timeout", "5", "app", "agent"])
        self.assertIn("default app from now on", result.stdout)

    def scripted_debug(self, state_failures=0, app_status=0):
        """A scripts/jelli-debug whose state fails the first ``state_failures`` times and
        whose ``app agent`` exits ``app_status``; each call is one line of its arguments.
        A fake sleep on the activated PATH records the waits instead of taking them."""
        log, count = self.root / "debug-calls", self.root / "debug-state-count"
        debug = self.repo / "scripts/jelli-debug"
        debug.write_text(
            "#!/bin/sh\n"
            f"for a in \"$@\"; do printf '%s\\037' \"$a\"; done >> '{log}'; printf '\\n' >> '{log}'\n"
            f"if [ \"$5\" = state ]; then n=$(cat '{count}' 2>/dev/null || echo 0); n=$((n + 1))\n"
            f"  echo $n > '{count}'; echo 'pet state on stdout'; [ $n -gt {state_failures} ]; exit $?; fi\n"
            f"echo 'app on stdout'; exit {app_status}\n")
        debug.chmod(0o755)
        sleeps = self.root / "sleeps"
        sleep = self.root / "bin/sleep"
        sleep.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$*\" >> '{sleeps}'\n")
        sleep.chmod(0o755)
        calls = lambda: [line.split("\x1f")[:-1] for line in log.read_text().splitlines()] if log.exists() else []
        waits = lambda: sleeps.read_text().splitlines() if sleeps.exists() else []
        return calls, waits

    STATE = ["--port", "/dev/cu.test", "--timeout", "2", "state"]
    APP = ["--port", "/dev/cu.test", "--timeout", "5", "app", "agent"]
    FALLBACK = "the pet is running and starts it at the next reset"

    def test_a_pet_that_never_answers_is_asked_ten_times_then_left_to_start_the_agent(self):
        calls, waits = self.scripted_debug(state_failures=99)
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls(), [self.STATE] * 10)  # never "app agent"
        self.assertEqual(waits(), ["1"] * 9)  # none after the last try
        self.assertIn(self.FALLBACK, result.stdout)
        self.assertNotIn("default app from now on", result.stdout)
        self.assertNotIn("pet state on stdout", result.stdout)

    def test_a_pet_that_answers_late_is_handed_the_agent_once(self):
        calls, waits = self.scripted_debug(state_failures=3)
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls(), [self.STATE] * 4 + [self.APP])
        self.assertEqual(waits(), ["1"] * 3)
        self.assertIn("default app from now on", result.stdout)
        self.assertNotIn("app on stdout", result.stdout)

    def test_a_refused_app_agent_is_not_retried_and_leaves_the_pet_running(self):
        calls, waits = self.scripted_debug(app_status=4)
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        # The agent is written and the pet starts it at its next reset (ADR-015), so the
        # command succeeds and says how to hand over by hand.
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls(), [self.STATE, self.APP])
        self.assertEqual(waits(), [])
        self.assertIn(self.FALLBACK, result.stdout)
        self.assertIn("scripts/jelli-debug --port PORT app agent", result.stdout)
        self.assertNotIn("default app from now on", result.stdout)

    def test_a_debug_client_that_is_not_executable_is_skipped(self):
        calls, waits = self.scripted_debug()
        (self.repo / "scripts/jelli-debug").chmod(0o644)
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((calls(), waits()), ([], []))
        self.assertIn(self.FALLBACK, result.stdout)

    def test_no_hand_over_after_a_failed_write_or_reset(self):
        for step in ("write_partition", "chip_id"):
            with self.subTest(step=step):
                calls, waits = self.scripted_debug()
                self.record_calls(fail_on=step)
                self.assertEqual(self.run_esp("flash-agent", "/dev/cu.test", app="agent").returncode, 3)
                self.assertEqual((calls(), waits()), ([], []))

    def test_unusual_ports_reach_the_debug_client_as_one_argument(self):
        for port in ("/dev/cu.my port", "-rf", "--help", "/dev/cu.*", "$(touch pwned)"):
            with self.subTest(port=port):
                (self.root / "debug-calls").unlink(missing_ok=True)
                calls, _ = self.scripted_debug()
                self.assertEqual(self.run_esp("flash-agent", port, app="agent").returncode, 0)
                self.assertEqual(calls(), [["--port", port, *self.STATE[2:]], ["--port", port, *self.APP[2:]]])
                self.assertFalse((self.repo / "pwned").exists() or Path("pwned").exists())

    def ordered_tools(self, fail_on=None):
        """Make idf.py and the SDK's python log to one file, in order, one call per line
        (arguments separated by US); a call exits 3 when an argument equals ``fail_on``."""
        log = self.root / "tool-calls"
        check = f'for a in "$@"; do [ "$a" = {shlex.quote(fail_on)} ] && exit 3; done\n' if fail_on else ""
        for name in ("idf.py", "python"):
            (self.root / "bin" / name).write_text(
                "#!/bin/sh\n"
                f"printf '%s\\037' {name} >> '{log}'\n"
                f"for a in \"$@\"; do printf '%s\\037' \"$a\"; done >> '{log}'\n"
                f"printf '\\n' >> '{log}'\n{check}exit 0\n")
        return lambda: [line.split("\x1f")[:-1] for line in log.read_text().splitlines()] if log.exists() else []

    def install_steps(self, port):
        """What install-agent runs: build, flash (layout A's table included), erase ota_0
        (staying in download mode), then one reset."""
        build = self.repo / "build/esp32-agent"
        idf = ["idf.py", "-C", str(self.repo / "ports/esp32-agent"), "-B", str(build),
               "-DIDF_TARGET=esp32s3", "-DJELLI_ESP_BOARD=1.75"]
        erase = ["python", str(self.repo / ".tools/esp-idf/components/partition_table/parttool.py"),
                 "--port", port, "--esptool-args=after=no_reset", "erase_partition", "--partition-name", "ota_0"]
        return [idf + ["build"], idf + ["-p", port, "flash"], erase, ["python", *START_APP(port)]]

    def test_install_agent_builds_flashes_then_empties_ota_0(self):
        # ADR-015: an older agent left in ota_0 would be started by a pet flashed later.
        calls = self.ordered_tools()
        result = self.run_esp("install-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls(), self.install_steps("/dev/cu.test"))
        self.assertIn("installed alone", result.stdout)

    def test_a_failed_install_step_stops_the_rest_and_claims_no_success(self):
        # A failed build flashes nothing; a failed flash erases nothing; a failed erase
        # is not hidden by a reset.
        for step, ran in (("build", 1), ("flash", 2), ("erase_partition", 3), ("chip_id", 4)):
            with self.subTest(step=step):
                (self.root / "tool-calls").unlink(missing_ok=True)
                calls = self.ordered_tools(fail_on=step)
                result = self.run_esp("install-agent", "/dev/cu.test", app="agent")
                self.assertEqual(result.returncode, 3)
                self.assertEqual(calls(), self.install_steps("/dev/cu.test")[:ran])
                self.assertNotIn("installed alone", result.stdout)

    def test_install_agent_passes_unusual_ports_as_one_argument(self):
        for port in ("/dev/cu.my port", "-rf", "--help", "/dev/cu.*", "$(touch pwned)", "flash"):
            with self.subTest(port=port):
                (self.root / "tool-calls").unlink(missing_ok=True)
                calls = self.ordered_tools()
                result = self.run_esp("install-agent", port, app="agent")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(calls(), self.install_steps(port))
                self.assertFalse((self.repo / "pwned").exists() or Path("pwned").exists())

    def test_install_agent_refuses_invalid_boards_before_any_tool(self):
        for board in INVALID:
            with self.subTest(board=board):
                result = self.run_esp("install-agent", "/dev/cu.test", board=board, app="agent")
                self.assertIn(f"Unknown JELLI_ESP_BOARD '{board}'", result.stderr)
                self.assert_refused(result, "expected 1.75 or 1.75c")

    def test_install_agent_never_asks_the_pet_to_hand_over(self):
        calls, waits = self.scripted_debug()
        result = self.run_esp("install-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((calls(), waits()), ([], []))

    def test_install_agent_writes_the_agent_alone_with_idf_flash(self):
        # The agent builds with layout A's table, so idf.py flash puts it in factory
        # and blanks otadata; no data partition is written.
        result = self.run_esp("install-agent", "/dev/cu.test", board="1.75c", app="agent")
        self.assert_board(result, "1.75c", "agent")
        self.assertEqual(self.idf_arguments()[-3:], ["-p", "/dev/cu.test", "flash"])
        self.assertIn("installed alone", result.stdout)

    def test_install_agent_requires_the_agent_app_and_one_port(self):
        self.assert_refused(self.run_esp("install-agent", "/dev/null"), "JELLI_ESP_APP=agent")
        self.assert_refused(self.run_esp("install-agent", app="agent"), "JELLI_ESP_APP=agent")
        self.assert_refused(self.run_esp("install-agent", "/dev/a", "/dev/b", app="agent"),
                            "JELLI_ESP_APP=agent")

    def test_boot_selects_an_app_through_otadata(self):
        for app, expected in (("pet", ["erase_otadata"]), ("agent", ["switch_ota_partition", "--slot", "0"])):
            with self.subTest(app=app):
                self.python_log.unlink(missing_ok=True)
                result = self.run_esp("boot", "/dev/cu.test", app)
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = self.python_log.read_text().splitlines()
                self.assertEqual(Path(calls[0]).name, "otatool.py")
                self.assertEqual(calls[1:], ["--port", "/dev/cu.test", "--esptool-args=after=no_reset",
                                             *expected, *START_APP("/dev/cu.test")])

    def test_boot_rejects_unknown_apps_and_missing_arguments(self):
        for arguments in (("/dev/cu.test", "factory"), ("/dev/cu.test",), ()):
            with self.subTest(arguments=arguments):
                self.python_log.unlink(missing_ok=True)
                result = self.run_esp("boot", *arguments)
                self.assertEqual(result.returncode, 2)
                self.assertIn("scripts/esp boot PORT pet|agent", result.stderr)
                self.assertFalse(self.python_log.exists(), "otatool ran")

    def test_port_commands_reject_invalid_boards_before_any_tool(self):
        for command, arguments, app in (("flash-agent", ("/dev/cu.test",), "agent"),
                                        ("boot", ("/dev/cu.test", "agent"), None),
                                        ("flash", ("/dev/cu.test",), None),
                                        ("monitor", ("/dev/cu.test",), "agent")):
            for board in ("1.76", "1.75c;rm", "-1.75"):
                with self.subTest(command=command, board=board):
                    result = self.run_esp(command, *arguments, board=board, app=app)
                    self.assertIn(f"Unknown JELLI_ESP_BOARD '{board}'", result.stderr)
                    self.assert_refused(result, "expected 1.75 or 1.75c")

    def test_unusual_ports_reach_each_tool_as_one_argument(self):
        # Quoting keeps spaces, globs, and leading dashes inside the --port/-p value.
        for port in ("/dev/cu.my port", "-rf", "--help", "/dev/cu.*", "$(touch pwned)"):
            with self.subTest(port=port):
                for log in (self.idf_log, self.python_log):
                    log.unlink(missing_ok=True)
                self.assertEqual(self.run_esp("boot", port, "pet").returncode, 0)
                self.assertEqual(self.python_log.read_text().splitlines()[1:],
                                 ["--port", port, "--esptool-args=after=no_reset", "erase_otadata", *START_APP(port)])
                self.python_log.unlink()
                self.assertEqual(self.run_esp("flash-agent", port, app="agent").returncode, 0)
                calls = self.python_log.read_text().splitlines()
                ports = [calls[index + 1] for index, value in enumerate(calls) if value == "--port"]
                self.assertEqual(ports, [port, port, port])  # otatool, parttool, then the reset
                self.assertEqual(self.run_esp("flash", port).returncode, 0)
                self.assertEqual(self.idf_arguments()[-3:], ["-p", port, "flash"])
                self.assertFalse((self.repo / "pwned").exists() or Path("pwned").exists())

    def test_monitor_of_the_agent_uses_the_agent_build(self):
        # The monitor decodes backtraces with the ELF in the build directory.
        result = self.run_esp("monitor", "/dev/cu.test", app="agent")
        self.assert_board(result, "1.75", "agent")
        self.assertEqual(self.idf_arguments()[-3:], ["-p", "/dev/cu.test", "monitor"])

    def fail_tool(self, path, log):
        path.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$@\" >> '{log}'\nexit 3\n")

    def test_a_failed_agent_build_never_writes_a_stale_image(self):
        self.fail_tool(self.root / "bin/idf.py", self.idf_log)
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.python_log.exists(), "parttool ran after a failed build")
        self.assertNotIn("Agent written", result.stdout)

    def test_flash_tool_failures_propagate(self):
        self.fail_tool(self.root / "bin/python", self.python_log)
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 3)
        self.assertNotIn("Agent written", result.stdout)
        # A failed boot-choice reset stops before anything is written to ota_0.
        self.assertNotIn("write_partition", self.python_log.read_text())
        for app in ("pet", "agent"):
            with self.subTest(app=app):
                self.assertEqual(self.run_esp("boot", "/dev/cu.test", app).returncode, 3)

    def record_calls(self, fail_on=None):
        """Replace the fake python with one that logs each call's argv on its own line.

        Arguments are separated by US (0x1f) so each argv element stays distinct; the
        call exits 3 when any argument equals ``fail_on``.
        """
        log = self.root / "python-calls"
        check = f'for a in "$@"; do [ "$a" = {shlex.quote(fail_on)} ] && exit 3; done\n' if fail_on else ""
        (self.root / "bin/python").write_text(
            "#!/bin/sh\n"
            f"for a in \"$@\"; do printf '%s\\037' \"$a\"; done >> '{log}'\n"
            f"printf '\\n' >> '{log}'\n{check}exit 0\n")
        return lambda: [line.split("\x1f")[:-1] for line in log.read_text().splitlines()] if log.exists() else []

    def assert_sdk_calls(self, calls, steps):
        """Each SDK tool call names its tool, keeps the board in download mode, and runs ``steps``."""
        self.assertEqual([Path(call[0]).name for call in calls], [tool for tool, _ in steps])
        for call, (_, step) in zip(calls, steps):
            # One argv element: split, otatool/parttool's nargs='+' would swallow the subcommand.
            self.assertEqual(call[1:], ["--port", "/dev/cu.test", "--esptool-args=after=no_reset", *step])
            self.assertNotIn("hard_reset", call)

    def test_flash_agent_resets_exactly_once_and_last(self):
        calls = self.record_calls()
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls()), 3)
        self.assert_sdk_calls(calls()[:2], [
            ("otatool.py", ["erase_otadata"]),
            ("parttool.py", ["write_partition", "--partition-name", "ota_0", "--input",
                             str(self.repo / "build/esp32-agent/loki_agent.bin")])])
        self.assertEqual(calls()[2], START_APP("/dev/cu.test"))

    def test_boot_resets_exactly_once_after_the_otadata_change(self):
        for app, step in (("pet", ["erase_otadata"]), ("agent", ["switch_ota_partition", "--slot", "0"])):
            with self.subTest(app=app):
                (self.root / "python-calls").unlink(missing_ok=True)
                calls = self.record_calls()
                self.assertEqual(self.run_esp("boot", "/dev/cu.test", app).returncode, 0)
                self.assertEqual(len(calls()), 2)
                self.assert_sdk_calls(calls()[:1], [("otatool.py", step)])
                self.assertEqual(calls()[1], START_APP("/dev/cu.test"))

    def test_a_failed_boot_choice_reset_stops_before_the_write_and_the_reset(self):
        calls = self.record_calls(fail_on="erase_otadata")
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 3)
        self.assert_sdk_calls(calls(), [("otatool.py", ["erase_otadata"])])
        self.assertNotIn("Agent written", result.stdout)

    def test_a_failed_ota_0_write_leaves_the_board_in_download_mode(self):
        # otadata is already erased, so the next manual reset starts the pet; no
        # automatic reset hides the failure or boots a partly written ota_0.
        calls = self.record_calls(fail_on="write_partition")
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 3)
        self.assertEqual([Path(call[0]).name for call in calls()], ["otatool.py", "parttool.py"])
        self.assertNotIn("Agent written", result.stdout)

    def test_a_failed_final_reset_fails_the_command(self):
        calls = self.record_calls(fail_on="chip_id")
        result = self.run_esp("flash-agent", "/dev/cu.test", app="agent")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(calls()[-1], START_APP("/dev/cu.test"))
        self.assertEqual(len(calls()), 3)
        self.assertNotIn("Agent written", result.stdout)

    def test_a_failed_boot_choice_skips_the_reset(self):
        for app, step in (("pet", ["erase_otadata"]), ("agent", ["switch_ota_partition", "--slot", "0"])):
            with self.subTest(app=app):
                (self.root / "python-calls").unlink(missing_ok=True)
                calls = self.record_calls(fail_on=step[0])
                self.assertEqual(self.run_esp("boot", "/dev/cu.test", app).returncode, 3)
                self.assert_sdk_calls(calls(), [("otatool.py", step)])

    def test_boot_with_any_unknown_app_runs_no_tool(self):
        calls = self.record_calls()
        for choice in ("", "Pet", "AGENT", "agent ", " pet", "factory", "ota_0", "--slot", "pet;agent", "agent\n"):
            with self.subTest(choice=choice):
                result = self.run_esp("boot", "/dev/cu.test", choice)
                self.assertEqual(result.returncode, 2)
                self.assertIn("scripts/esp boot PORT pet|agent", result.stderr)
                self.assertEqual(calls(), [], "a flash tool ran")
                self.assertFalse(self.idf_log.exists(), "idf.py ran")

    def test_help_names_the_board_variable(self):
        result = self.run_esp("help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("JELLI_ESP_BOARD=1.75c", result.stdout)
        self.assertFalse(self.activated.exists())


class EspCMakeBoardTests(unittest.TestCase):
    """Configure each ESP project without an SDK; validation runs before IDF loads."""

    def configure(self, board, project=ESP_PORT):
        with tempfile.TemporaryDirectory(prefix="jelli-esp-cmake-") as directory:
            environment = dict(os.environ, IDF_PATH=str(Path(directory) / "missing-sdk"))
            return subprocess.run(
                [os.environ.get("JELLI_CMAKE", "cmake"), "-S", str(project), "-B", str(Path(directory) / "build"), f"-DJELLI_ESP_BOARD={board}"],
                env=environment, capture_output=True, text=True, timeout=60,
            )

    def test_unknown_board_stops_configuration(self):
        for project in (ESP_PORT, AGENT_PORT):
            for board in INVALID + ("",):
                with self.subTest(project=project.name, board=board):
                    result = self.configure(board, project)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Unknown JELLI_ESP_BOARD", result.stderr)
                    self.assertNotIn("project.cmake", result.stderr, "validation must stop before the SDK loads")

    def test_known_boards_pass_validation_and_reach_the_sdk(self):
        for project in (ESP_PORT, AGENT_PORT):
            for board in BOARDS:
                with self.subTest(project=project.name, board=board):
                    result = self.configure(board, project)
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
        agent = (AGENT_PORT / "CMakeLists.txt").read_text()
        self.assertEqual(re.search(r"set\(jelli_esp_boards ([^)]*)\)", agent).group(1), listed)
        self.assertIn('set(JELLI_ESP_BOARD "1.75" CACHE', agent)
        script = (REPOSITORY / "scripts/esp").read_text()
        self.assertIn('1.75) build_dir="$repo_dir/build/esp32$app_suffix" ;;', script)
        self.assertIn('1.75c) build_dir="$repo_dir/build/esp32$app_suffix-1.75c" ;;', script)

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

    def test_agent_locks_resolve_each_board_pin_and_the_shared_lvgl(self):
        lvgl_pin = re.search(r'lvgl/lvgl: "==([^"]+)"', (AGENT_PORT / "main/idf_component.yml").read_text()).group(1)
        pet_lvgl = re.search(r'lvgl/lvgl: "==([^"]+)"', (ESP_PORT / "main/idf_component.yml").read_text()).group(1)
        self.assertEqual(lvgl_pin, pet_lvgl)
        for board in BOARDS:
            with self.subTest(board=board):
                manifest = (ESP_PORT / "boards" / board / "jelli_board/idf_component.yml").read_text()
                bsp, version = re.search(r"^  (waveshare/[^:]+):\n    version: \"==([^\"]+)\"", manifest, re.M).groups()
                lock = (AGENT_PORT / f"dependencies.{board}.lock").read_text()
                self.assertEqual(lock_version(lock, bsp), version)
                self.assertEqual(lock_version(lock, "lvgl/lvgl"), lvgl_pin)


if __name__ == "__main__":
    unittest.main()
