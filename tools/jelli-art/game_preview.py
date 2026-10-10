"""Jelli Art "Test in game": render a scenario through the real C engine.

The page sends a scenario (pet and form, page, behaviour state, needs, potty,
mess, sleep, moment, clock, frame timing). The server mirrors the served
checkout's engine sources plus the served art and content into a staging tree
under the build cache, builds tools/game-preview/preview.c against jelli_core
and jelli_pet with CMake (the same generators the game uses, so content and
art errors surface exactly as in a real build), runs it, and returns PNG frames.

Everything is bounded: one build at a time with a timeout, at most two renders
in flight, at most MAX_FRAMES frames, and clear errors when cmake or a C
compiler is missing (the container image ships cmake only).
"""
import base64
import filecmp
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from http import HTTPStatus
from pathlib import Path

from PIL import Image

import behaviors

MAX_FRAMES = 48
MAX_STEP_MS = 10000
MAX_START_MS = 86400000
MAX_ADVANCE_S = 3600
BUILD_TIMEOUT_S = 300
RENDER_TIMEOUT_S = 30
MAX_RENDERS = 2
LOG_LINES = 120
NEEDS = ("satiety", "energy", "hygiene", "amusement", "social")
# Engine sources mirrored from the served checkout; art and content come from what the studio serves.
STAGE_FILES = ("CMakeLists.txt", "VERSION", "toolchain.env")
STAGE_DIRS = ("cmake", "core", "include", "tools/assets", "tools/audio", "tools/game-preview")
SKIP = re.compile(r"(__pycache__|\.pyc$|\.tmp$|\.DS_Store$)")
TARGET = "jelli_game_preview"


class ScenarioError(ValueError):
    """A scenario the page must fix; carries one message per field."""

    def __init__(self, errors):
        super().__init__("; ".join(f"{e['field']}: {e['message']}" for e in errors))
        self.errors = errors


class PreviewUnavailable(RuntimeError):
    """This host cannot build the engine (no cmake, no compiler, no checkout sources)."""


class BuildFailed(RuntimeError):
    """The engine build or the generators rejected the working copy."""

    def __init__(self, stage, messages, log):
        super().__init__(f"{stage}: {'; '.join(messages)}")
        self.stage, self.messages, self.log = stage, messages, log


# ---------- catalogue and scenario validation ----------

def catalogue(repo, content):
    """Pages, pets, forms, states and moments the scenario may name, from the engine and content."""
    enums = behaviors.c_enums([Path(repo) / "include/jelli/pet_ui.h"]) if (Path(repo) / "include/jelli/pet_ui.h").exists() else {}
    pages = sorted(((v, k.removeprefix("JELLI_UI_").lower()) for k, v in enums.items()
                    if k.startswith("JELLI_UI_") and not k.startswith("JELLI_UI_ACTION_") and k != "JELLI_UI_PAGE_COUNT"))
    ring_limit = enums.get("JELLI_UI_BRUSH", 0)
    collection = enums.get("JELLI_UI_COLLECTION")
    pets = behaviors.read_json(Path(content) / "pets.json") or {}
    forms = {f.get("id"): f.get("name") for f in pets.get("forms", [])}
    sets = {s.get("id"): s.get("forms", []) for s in pets.get("evolution_sets", [])}
    entries = [{"id": e.get("id"), "name": e.get("name"),
                "forms": [{"id": f, "name": forms.get(f, str(f))} for f in sets.get(e.get("evolution_set"), [])]}
               for e in pets.get("entries", [])]
    states = [s.get("name") for s in (behaviors.read_json(Path(content) / "behaviors.json") or {}).get("states", [])]
    moments = [m.get("name") for m in (behaviors.read_json(Path(content) / "activities.json") or {}).get("moments", [])]
    return {"pages": [{"id": v, "name": n, "ring": v < ring_limit and v != collection} for v, n in pages],
            "entries": entries, "states": states, "moments": moments, "needs": list(NEEDS),
            "limits": {"frames": MAX_FRAMES, "step_ms": MAX_STEP_MS, "start_ms": MAX_START_MS,
                       "advance_s": MAX_ADVANCE_S, "need": 1000, "potty": 1000, "minute": 1439}}


def _integer(raw, field, low, high, default, errors, prefix=""):
    value = raw.get(field, default)
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        errors.append({"field": prefix + field, "message": f"must be a whole number {low}..{high}"})
        return default
    return value


def _flag(raw, field, errors):
    value = raw.get(field, False)
    if not isinstance(value, bool):
        errors.append({"field": field, "message": "must be true or false"})
        return False
    return value


def _named(raw, field, names, errors, optional=True):
    """Index of a name in names (case-insensitive), or None for ''/missing when optional."""
    value = raw.get(field, "")
    if value in ("", None) and optional:
        return None
    if isinstance(value, str):
        folded = [str(n).lower() for n in names]
        if value.lower() in folded:
            return folded.index(value.lower())
    errors.append({"field": field, "message": f"unknown {field} {value!r}; expected one of: {', '.join(map(str, names))}"})
    return None


def _pet(raw, cat, errors):
    entries = cat["entries"]
    index = _named(raw, "pet", [e["name"] for e in entries], errors, optional=True)
    entry = entries[index if index is not None else 0] if entries else None
    if entry is None:
        errors.append({"field": "pet", "message": "content/pets.json has no entries"})
        return None, None
    forms = entry["forms"]
    if not forms:
        errors.append({"field": "pet", "message": f"{entry['name']} has no evolution set forms in content/pets.json"})
        return entry, None
    if raw.get("form") in ("", None):
        return entry, forms[0] if forms else None
    pick = _named(raw, "form", [f["name"] for f in forms], [], optional=False)
    if pick is None:
        errors.append({"field": "form", "message": f"{entry['name']} grows through {', '.join(f['name'] for f in forms)}, not {raw.get('form')!r}"})
        return entry, None
    return entry, forms[pick]


def parse_scenario(raw, cat):
    """Validate a scenario dict against the catalogue; returns (argv, summary)."""
    if not isinstance(raw, dict):
        raise ScenarioError([{"field": "scenario", "message": "must be a JSON object"}])
    errors = []
    entry, form = _pet(raw, cat, errors)
    page_names = [p["name"] for p in cat["pages"]]
    page = _named(raw, "page", page_names, errors) or 0
    state = _named(raw, "behavior", cat["states"], errors)
    moment = _named(raw, "moment", cat["moments"], errors)
    needs_raw = raw.get("needs", {})
    if not isinstance(needs_raw, dict):
        errors.append({"field": "needs", "message": "must map need names to 0..1000"})
        needs_raw = {}
    unknown = sorted(set(needs_raw) - set(NEEDS))
    if unknown:
        errors.append({"field": "needs", "message": f"unknown needs {', '.join(unknown)}"})
    needs = [_integer(needs_raw, n, 0, 1000, 700, errors, "needs.") for n in NEEDS]
    s = {"potty": _integer(raw, "potty", 0, 1000, 0, errors), "minute": _integer(raw, "minute", -1, 1439, -1, errors),
         "start_ms": _integer(raw, "start_ms", 0, MAX_START_MS, 0, errors),
         "step_ms": _integer(raw, "step_ms", 1, MAX_STEP_MS, 200, errors),
         "frames": _integer(raw, "frames", 1, MAX_FRAMES, 12, errors),
         "advance_s": _integer(raw, "advance_s", 0, MAX_ADVANCE_S, 0, errors),
         "menu": _flag(raw, "menu", errors), "mess": _flag(raw, "mess", errors),
         "asleep": _flag(raw, "asleep", errors), "live": _flag(raw, "live", errors)}
    if s["asleep"] and moment is not None:
        errors.append({"field": "moment", "message": "a sleeping pet cannot run a moment; wake it or clear the moment"})
    if s["menu"] and not cat["pages"][page]["ring"]:
        errors.append({"field": "menu", "message": f"the {page_names[page]} page has no ring menu"})
    if errors:
        raise ScenarioError(errors)
    argv = ["--entry", entry["id"], "--form", form["id"], "--page", page, "--needs", ",".join(map(str, needs)),
            "--behavior", 0 if state is None else state + 1, "--moment", 0 if moment is None else moment + 1,
            *[x for key in ("potty", "minute", "start_ms", "step_ms", "frames", "advance_s") for x in (f"--{key}", s[key])],
            *[x for key in ("menu", "mess", "asleep", "live") for x in (f"--{key}", int(s[key]))]]
    summary = {"pet": entry["name"], "form": form["name"], "page": page_names[page],
               "behavior": cat["states"][state] if state is not None else "", "moment": cat["moments"][moment] if moment is not None else "",
               "needs": dict(zip(NEEDS, needs)), **s}
    return [str(a) for a in argv], summary


def alt_text(summary, index):
    """What a frame shows, for screen readers and the image title."""
    s = summary
    parts = [f"{s['pet']} as {s['form']}", f"on the {s['page'].replace('_', ' ')} page" + (" with the ring menu open" if s["menu"] else "")]
    parts += [f"behaviour {s['behavior']}"] if s["behavior"] else []
    parts += [f"running {s['moment']}"] if s["moment"] else []
    parts += ["asleep"] if s["asleep"] else []
    parts += ["a mess on the floor"] if s["mess"] else []
    parts += [f"potty urge {s['potty']}"] if s["potty"] else []
    parts.append("needs " + ", ".join(f"{k} {v}" for k, v in s["needs"].items()))
    if s["minute"] >= 0:
        parts.append(f"clock {s['minute'] // 60:02d}:{s['minute'] % 60:02d}")
    when = s["start_ms"] + index * s["step_ms"]
    return f"Engine render, frame {index + 1} of {s['frames']} at {when} ms: " + "; ".join(parts) + "."


# ---------- bounded subprocesses and build output ----------

def run_bounded(cmd, cwd, timeout, env=None):
    """Run in its own process group so a timeout also stops compilers it started."""
    proc = subprocess.Popen([str(c) for c in cmd], cwd=cwd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            proc.kill()
        proc.communicate()
        return None, f"timed out after {timeout} s"
    return proc.returncode, out


def build_messages(output, root):
    """(stage, messages) from cmake/generator/compiler output, without call stacks or paths."""
    text = output.replace(str(root) + os.sep, "")
    if "No CMAKE_C_COMPILER could be found" in text or "CMAKE_C_COMPILER not set" in text:
        return "tools", ["No C compiler is installed here, so the engine cannot be built for a preview."]
    if "CMake Error" in text:
        message = behaviors.cmake_messages(text)
        return ("content" if re.search(r"content/|\.json|must be|Unknown|expected", message) else "configure"), [message]
    raised = [m.group(2) for m in re.finditer(r"^(\w*(?:Error|Exception)): (.+)$", text, re.M)]
    if raised:
        return "art", list(dict.fromkeys(raised))
    compiled = [line.strip() for line in text.splitlines() if ": error:" in line or ": fatal error:" in line]
    if compiled:
        return "compile", compiled[:12]
    tail = [line for line in text.splitlines() if line.strip()][-8:]
    return "build", tail or ["the build failed without output"]


def sync_tree(src, dst):
    """Mirror src into dst, copying only changed files so builds stay incremental. True if anything changed."""
    src, dst = Path(src), Path(dst)
    changed, seen = False, set()
    for path in sorted(src.rglob("*")):
        rel = path.relative_to(src)
        if SKIP.search(str(rel)) or not path.is_file():
            continue
        seen.add(rel)
        target = dst / rel
        if target.is_file() and filecmp.cmp(path, target, shallow=False):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        changed = True
    if dst.exists():
        for path in sorted(dst.rglob("*"), reverse=True):
            rel = path.relative_to(dst)
            if path.is_file() and rel not in seen and not SKIP.search(str(rel)):
                path.unlink()
                changed = True
    return changed


def frames_to_png(directory, count):
    """PPM frames from the C tool to base64 PNG data URLs."""
    urls = []
    for index in range(count):
        with Image.open(Path(directory) / f"frame-{index:03d}.ppm") as image:
            buffer = io.BytesIO()
            image.convert("RGB").save(buffer, "PNG", optimize=False)
        urls.append("data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode())
    return urls


# ---------- the preview service ----------

def default_cache(repo):
    """build/jelli-art-preview in the checkout, or a temp directory when the checkout is read-only."""
    override = os.environ.get("JELLI_PREVIEW_CACHE")
    if override:
        return Path(override)
    preferred = Path(repo) / "build"
    try:
        preferred.mkdir(exist_ok=True)
        if os.access(preferred, os.W_OK):
            return preferred / "jelli-art-preview"
    except OSError:
        pass
    return Path(tempfile.gettempdir()) / "jelli-art-preview"


class GamePreview:
    def __init__(self, repo, source, content, cache=None):
        self.repo, self.source, self.content = Path(repo), Path(source), Path(content)
        self.cache = Path(cache) if cache else default_cache(repo)
        self.stage, self.build_dir = self.cache / "src", self.cache / "build"
        self.build_lock = threading.Lock()
        self.renders = threading.BoundedSemaphore(MAX_RENDERS)
        self.built = None  # None: unknown; True: the staged tree built; BuildFailed: it did not
        self.last_build = {}

    def missing(self):
        """Why this host cannot build the engine, or None."""
        if not shutil.which("cmake"):
            return "cmake is not installed here, so Test in game cannot build the engine."
        if not (os.environ.get("CC") or any(shutil.which(c) for c in ("cc", "gcc", "clang"))):
            return "No C compiler (cc, gcc or clang) is installed here, so Test in game cannot build the engine."
        if not (self.repo / "tools/game-preview/preview.c").exists():
            return "This checkout has no tools/game-preview/preview.c."
        return None

    def status(self):
        return {"available": self.missing() is None, "reason": self.missing(), "last_build": self.last_build,
                **catalogue(self.repo, self.content)}

    def _stage(self):
        changed = False
        for name in STAGE_FILES:
            target = self.stage / name
            if not (target.is_file() and filecmp.cmp(self.repo / name, target, shallow=False)):
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.repo / name, target)
                changed = True
        for name in STAGE_DIRS:
            changed |= sync_tree(self.repo / name, self.stage / name)
        changed |= sync_tree(self.source, self.stage / "assets/slice")
        changed |= sync_tree(self.content, self.stage / "content")
        # Generators run with the studio's own Python, which already has Pillow; no uv download.
        shim = self.stage / "scripts/uv"
        body = ('#!/usr/bin/env bash\n# Jelli Art preview: run `uv run ... script.py args` with the studio Python.\n'
                'while [[ $# -gt 0 && "$1" != *.py ]]; do shift; done\n'
                f'PYTHONDONTWRITEBYTECODE=1 exec "{sys.executable}" "$@"\n')
        if not shim.is_file() or shim.read_text() != body:
            shim.parent.mkdir(parents=True, exist_ok=True)
            shim.write_text(body)
            shim.chmod(0o755)
            changed = True
        return changed

    def _configure_args(self):
        args = ["cmake", "-S", self.stage, "-B", self.build_dir, "-DJELLI_BUILD_SDL=OFF", "-DJELLI_BUILD_PET=ON",
                "-DBUILD_TESTING=OFF", "-DCMAKE_BUILD_TYPE=Release"]
        if shutil.which("ninja") and not (self.build_dir / "CMakeCache.txt").exists():
            args[1:1] = ["-G", "Ninja"]
        return args

    def binary(self):
        return self.build_dir / (TARGET + (".exe" if os.name == "nt" else ""))

    def ensure_built(self):
        """Stage the working copy and build if it changed. Returns (binary, rebuilt)."""
        reason = self.missing()
        if reason:
            raise PreviewUnavailable(reason)
        with self.build_lock:
            changed = self._stage()
            if not changed and self.built is True and self.binary().is_file():
                return self.binary(), False
            if not changed and isinstance(self.built, BuildFailed):
                raise self.built
            started = time.monotonic()
            log = []
            for cmd in (self._configure_args(), ["cmake", "--build", self.build_dir, "--target", TARGET, "--parallel"]):
                code, out = run_bounded(cmd, self.cache, BUILD_TIMEOUT_S)
                log.extend(out.replace(str(self.cache) + os.sep, "").splitlines()[-LOG_LINES:])
                if code is None:
                    self.built = BuildFailed("timeout", [f"The engine build {out}."], log[-LOG_LINES:])
                    break
                if code:
                    stage, messages = build_messages(out, self.stage)
                    self.built = BuildFailed(stage, messages, log[-LOG_LINES:])
                    break
            else:
                self.built = True
            self.last_build = {"ok": self.built is True, "seconds": round(time.monotonic() - started, 1),
                               "at": time.strftime("%H:%M:%S")}
            if self.built is not True:
                raise self.built
            return self.binary(), True

    def render(self, raw):
        argv, summary = parse_scenario(raw, catalogue(self.repo, self.content))
        if not self.renders.acquire(blocking=False):
            raise PreviewUnavailable("Two renders are already running; try again in a moment.")
        try:
            built, rebuilt = self.ensure_built()
            with tempfile.TemporaryDirectory(prefix="jelli-preview-") as out:
                binary = Path(out) / built.name
                with self.build_lock:  # a concurrent rebuild must not replace the binary mid-run
                    shutil.copy2(built, binary)
                code, text = run_bounded([binary, *argv, "--out", out], out, RENDER_TIMEOUT_S)
                if code is None:
                    raise BuildFailed("render", [f"The engine render {text}."], [])
                report = _last_json(text)
                if code or not report.get("ok"):
                    raise BuildFailed("scenario", [report.get("error") or text.strip()[-300:] or "render failed"], [])
                frames = frames_to_png(out, summary["frames"])
        finally:
            self.renders.release()
        return {"ok": True, "frames": frames, "alt": [alt_text(summary, i) for i in range(len(frames))],
                "times_ms": [summary["start_ms"] + i * summary["step_ms"] for i in range(len(frames))],
                "scenario": summary, "engine": report, "rebuilt": rebuilt, "last_build": self.last_build}


def _last_json(text):
    for line in reversed(text.splitlines()):
        if line.startswith("{"):
            try:
                return json.loads(line)
            except ValueError:
                break
    return {}


# ---------- HTTP glue (registered from jelli_art.py) ----------

SERVICE = None


def configure(repo, source, content, cache=None):
    global SERVICE
    SERVICE = GamePreview(repo, source, content, cache)
    return SERVICE


def route(handler, method, path):
    """Serve /api/game-preview; True when handled."""
    if path != "/api/game-preview" or SERVICE is None:
        return False
    if method == "GET":
        handler.send(HTTPStatus.OK, SERVICE.status())
        return True
    body = handler.body()
    try:
        handler.send(HTTPStatus.OK, SERVICE.render(body.get("scenario") if isinstance(body, dict) else None))
    except ScenarioError as error:
        handler.send(HTTPStatus.BAD_REQUEST, {"ok": False, "stage": "scenario", "error": str(error), "errors": error.errors})
    except PreviewUnavailable as error:
        handler.send(HTTPStatus.SERVICE_UNAVAILABLE, {"ok": False, "stage": "unavailable", "error": str(error)})
    except BuildFailed as error:
        # The working copy is wrong, not the request: report it as data so the page can show it inline.
        handler.send(HTTPStatus.OK, {"ok": False, "stage": error.stage, "error": "; ".join(error.messages),
                                     "messages": error.messages, "log": error.log[-40:], "last_build": SERVICE.last_build})
    return True
