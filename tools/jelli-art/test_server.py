# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""End-to-end tests for the Jelli Art server, without a browser.

Each test talks HTTP to a real jelli_art.py process serving scratch copies of
assets/slice and content/, so the checkout is never written. The git tests build
a small scratch checkout (art, content and the validators it needs) with a bare
"origin", and exercise commits, queued retries, crash recovery, pushes, push
repair and the container entrypoint.

Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_server.py
"""
import base64
import faulthandler
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SERVER = HERE / "jelli_art.py"
HAS_CMAKE = shutil.which("cmake") is not None
HAS_GIT = shutil.which("git") is not None
CHECKOUT_PARTS = (".gitignore", "assets/slice", "content", "tools/assets", "cmake/JelliBehaviors.cmake", "include/jelli")


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def git(repo, *args, check=True):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=False,
                            env={**os.environ, **GIT_ENV})
    if check and result.returncode:
        raise AssertionError(f"git {' '.join(args)}: {result.stderr}")
    return result.stdout.strip()


GIT_ENV = {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.com", "GIT_COMMITTER_NAME": "Test",
           "GIT_COMMITTER_EMAIL": "test@example.com", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}


class Server:
    """A jelli_art.py process on a free port; stopped by stop() or the test's cleanup."""

    def __init__(self, args, env=None, cwd=None, command=None):
        self.port = free_port()
        self.url = f"http://127.0.0.1:{self.port}"
        command = command or [sys.executable, str(SERVER), "--no-open", "--port", str(self.port), *args]
        self.log = tempfile.TemporaryFile()
        self.proc = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT, cwd=cwd,
                                     env={**os.environ, **GIT_ENV, **(env or {}), "JELLI_PORT": str(self.port)})
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise AssertionError(f"server exited: {self.output()}")
            try:
                self.get("/healthz")
                return
            except OSError:
                time.sleep(0.1)
        self.stop()
        raise AssertionError(f"server did not answer /healthz: {self.output()}")

    def output(self):
        if self.log.closed:
            return ""
        self.log.seek(0)
        return self.log.read().decode(errors="replace")

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if not self.log.closed:
            self.log.close()

    def request(self, method, path, body=None, headers=None, raw=None):
        """(status, decoded JSON) for any response, including 4xx and 5xx."""
        data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        sent = {"Content-Type": "application/json"} if data is not None else {}
        request = urllib.request.Request(self.url + path, data=data, method=method, headers={**sent, **(headers or {})})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.status, json.loads(response.read() or b"null")
        except urllib.error.HTTPError as error:
            text = error.read()
            try:
                return error.code, json.loads(text)
            except ValueError:
                return error.code, {"error": text.decode(errors="replace")}

    def get(self, path):
        status, body = self.request("GET", path)
        if status != 200:
            raise AssertionError(f"GET {path}: {status} {body}")
        return body

    def post(self, path, body, expect=200):
        status, reply = self.request("POST", path, body)
        if status != expect:
            raise AssertionError(f"POST {path}: expected {expect}, got {status} {reply}\n{self.output()[-2000:]}")
        return reply


def copy_tree(dest):
    """Scratch copies of assets/slice and content/."""
    shutil.copytree(REPO / "assets/slice", dest / "slice", ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(REPO / "content", dest / "content")
    return dest / "slice", dest / "content"


def make_checkout(root):
    """A minimal git checkout the studio can serve and commit to, plus a bare origin holding main."""
    work, origin = root / "work", root / "origin.git"
    for part in CHECKOUT_PARTS:
        src, dst = REPO / part, work / part
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__"))
        else:
            shutil.copy(src, dst)
    subprocess.run(["git", "init", "--quiet", "--bare", "--initial-branch=main", str(origin)], check=True)
    git(work.parent, "init", "--quiet", "--initial-branch=main", str(work))
    git(work, "add", "-A")
    git(work, "commit", "--quiet", "-m", "initial")
    git(work, "remote", "add", "origin", str(origin))
    git(work, "push", "--quiet", "origin", "main")
    git(work, "checkout", "--quiet", "-b", "art/studio")
    return work, origin


def first_editable(data, kind="creatures", skip=()):
    return next(a for a in data["assets"] if a["kind"] == kind and a["palette_name"] == "shared" and a["key"] not in skip)


def pixels_of(path):
    image = Image.open(path).convert("RGBA")
    return [f"#{r:02x}{g:02x}{b:02x}" if a else None for r, g, b, a in image.getdata()]


def repaint(pixels, colour="#ffffff", index=None):
    """A copy of pixels with one opaque pixel recoloured (or pixel `index`)."""
    out = list(pixels)
    i = index if index is not None else next(i for i, p in enumerate(out) if p and p != colour)
    out[i] = colour
    return out


def wait_for(predicate, timeout=30, message="condition"):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.2)
    raise AssertionError(f"timed out waiting for {message}")


class ServerTest(unittest.TestCase):
    """One server on scratch copies; each test edits different assets."""

    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="jelli-art-e2e-")
        cls.slice, cls.content = copy_tree(Path(cls.scratch.name))
        cls.server = Server(["--assets", str(cls.slice), "--content", str(cls.content)])
        cls.data = cls.server.get("/api/data")
        cls.used = set()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()
        cls.scratch.cleanup()

    def asset(self, kind="creatures"):
        asset = first_editable(self.data, kind, skip=self.used)
        self.used.add(asset["key"])
        return asset, self.slice / next(a["path"] for a in json.loads((self.slice / "assets.json").read_text())["assets"]
                                         if a["key"] == asset["key"])

    def test_paint_save_round_trip(self):
        asset, path = self.asset()
        pixels = repaint(pixels_of(path))
        reply = self.server.post("/api/save", {"key": asset["key"], "pixels": pixels, "base": asset["sha"], "artist": "e2e"})
        self.assertNotEqual(reply["sha"], asset["sha"])
        self.assertEqual(pixels_of(path), pixels)
        after = next(a for a in self.server.get("/api/data")["assets"] if a["key"] == asset["key"])
        self.assertEqual(after["sha"], reply["sha"])
        self.assertTrue(after["hand_painted"])
        self.assertEqual(self.server.get("/api/version")["version"], reply["version"])

    def test_paint_stale_base_is_refused_and_nothing_changes(self):
        asset, path = self.asset()
        before = path.read_bytes()
        status, reply = self.server.request("POST", "/api/save", {"key": asset["key"], "pixels": repaint(pixels_of(path)),
                                                                    "base": "0000000000000000"})
        self.assertEqual(status, 409, reply)
        self.assertTrue(reply.get("stale"))
        self.assertEqual(reply["current"], asset["sha"])  # lets the page overwrite after asking
        self.assertEqual(path.read_bytes(), before)

    def test_second_save_with_old_base_is_refused(self):
        asset, path = self.asset()
        first = repaint(pixels_of(path), "#ffffff")
        self.server.post("/api/save", {"key": asset["key"], "pixels": first, "base": asset["sha"]})
        status, _ = self.server.request("POST", "/api/save", {"key": asset["key"], "pixels": repaint(first, "#000000"),
                                                               "base": asset["sha"]})
        self.assertEqual(status, 409)
        self.assertEqual(pixels_of(path), first)

    def test_save_without_base_is_still_accepted(self):
        asset, path = self.asset()
        self.server.post("/api/save", {"key": asset["key"], "pixels": repaint(pixels_of(path))})

    def test_bad_requests_get_clear_4xx(self):
        asset, path = self.asset()
        pixels = pixels_of(path)
        cases = [
            ("not json", None, 400, "not valid JSON"),
            ("[]", None, 400, "JSON object"),
            ('{"key": "x"}', None, 400, "Missing field 'pixels'"),
            (json.dumps({"key": 5, "pixels": []}), None, 400, "'key' must be a string"),
            (json.dumps({"key": "nope", "pixels": []}), None, 400, "Unknown asset"),
            (json.dumps({"key": asset["key"], "pixels": pixels[:-1]}), None, 400, "Expected a list"),
            (json.dumps({"key": asset["key"], "pixels": ["#zzzzzz"] * len(pixels)}), None, 400, "must be '#rrggbb'"),
            (json.dumps({"key": asset["key"], "pixels": [None] * len(pixels)}), None, 400, "opaque pixel"),
            ('{"key": "x", "pixels": []}', {"Content-Type": "text/plain"}, 415, "application/json"),
            ("{}", {"Content-Length": "abc"}, 400, "Content-Length"),
        ]
        for raw, headers, status, text in cases:
            with self.subTest(raw=raw[:40], headers=headers):
                got, reply = self.server.request("POST", "/api/save", raw=raw.encode(), headers=headers)
                self.assertEqual(got, status, reply)
                self.assertIn(text, reply["error"])
        got, reply = self.server.request("POST", "/api/save", raw=b"x" * ((1 << 20) + 1))
        self.assertEqual(got, 413, reply)
        self.assertEqual(self.server.request("PUT", "/api/save", raw=b"{}")[0], 405)
        self.assertEqual(self.server.request("GET", "/api/nope")[0], 404)
        for body, text in (({"index": True, "color": "#123456"}, "integer"), ({"index": 0, "color": "red"}, "#rrggbb"),
                           ({"index": 999, "color": "#123456"}, "Unknown palette slot")):
            got, reply = self.server.request("POST", "/api/palette", body)
            self.assertEqual(got, 400, reply)
            self.assertIn(text, reply["error"])
        self.assertEqual(path.read_bytes(), path.read_bytes())

    def test_concurrent_saves_to_different_assets_all_land(self):
        targets = [self.asset() for _ in range(6)]
        results = []

        def save(asset, path):
            body = {"key": asset["key"], "pixels": repaint(pixels_of(path)), "base": asset["sha"]}
            results.append(self.server.request("POST", "/api/save", body))
        threads = [threading.Thread(target=save, args=t) for t in targets]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual([s for s, _ in results], [200] * len(targets), results)
        painted = json.loads((self.slice / "source/hand-painted.json").read_text())
        for asset, _ in targets:  # no lost update of the shared hand-painted list or manifest
            self.assertIn(asset["key"], painted)
        json.loads((self.slice / "assets.json").read_text())

    def test_concurrent_saves_to_one_asset_admit_exactly_one(self):
        asset, path = self.asset()
        pixels = pixels_of(path)
        versions = [repaint(pixels, f"#0000{i:02x}") for i in range(8)]
        results = []
        threads = [threading.Thread(target=lambda v=v: results.append(
            (v, self.server.request("POST", "/api/save", {"key": asset["key"], "pixels": v, "base": asset["sha"]})[0])))
            for v in versions]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        winners = [v for v, status in results if status == 200]
        self.assertEqual(len(winners), 1, [s for _, s in results])
        self.assertEqual(sorted(s for _, s in results), [200] + [409] * 7)
        self.assertEqual(pixels_of(path), winners[0])

    def test_no_temporary_files_remain(self):
        leftovers = [p for p in self.slice.rglob("*") if ".jelli-art-tmp-" in p.name or p.name.endswith(".tmp")]
        self.assertEqual(leftovers, [])

    def test_clip_save_and_stale_clip_base(self):
        data = self.server.get("/api/data")
        clip = next(c for c in data["clips"] if len(c["frames"]) > 1)
        edit = {"key": clip["key"], "frames": clip["frames"], "durations_ms": [d + 10 for d in clip["durations_ms"]],
                "loop": clip["loop"]}
        base = data["clip_shas"][clip["key"]]
        reply = self.server.post("/api/clips", {"clips": [edit], "bases": {clip["key"]: base}})
        self.assertEqual(reply["changed"], [clip["key"]])
        self.assertNotEqual(reply["clip_shas"][clip["key"]], base)
        edit["durations_ms"] = [d + 20 for d in clip["durations_ms"]]
        status, reply = self.server.request("POST", "/api/clips", {"clips": [edit], "bases": {clip["key"]: base}})
        self.assertEqual(status, 409, reply)
        status, reply = self.server.request("POST", "/api/clips", {"clips": [edit], "bases": [base]})
        self.assertEqual(status, 400, reply)

    @unittest.skipUnless(HAS_CMAKE, "cmake validates behaviour edits")
    def test_behaviour_save_and_stale_refusal(self):
        data = self.server.get("/api/behaviour")
        doc = data["behavior_data"]
        before = data["behavior_data_sha"]
        bases = {"behaviors": before, "creatures": data["creature_data_sha"]}
        reply = self.server.post("/api/content", {"docs": {"behaviors": doc}, "bases": bases})
        self.assertEqual(reply["changed"], [])  # unchanged documents write nothing
        path = self.content / "behaviors.json"
        on_disk = json.loads(path.read_text())
        path.write_text(json.dumps(on_disk, indent=4) + "\n")  # someone else edits the file
        status, reply = self.server.request("POST", "/api/content", {"docs": {"behaviors": doc}, "bases": bases})
        self.assertEqual(status, 409, reply)
        status, reply = self.server.request("POST", "/api/content", {"docs": {"behaviors": []}, "bases": bases})
        self.assertIn(status, (400, 409), reply)

    def test_creature_data_save_round_trip(self):
        data = self.server.get("/api/creatures")
        doc, sha = data["creature_data"], data["creature_data_sha"]
        reply = self.server.post("/api/creatures", {"data": doc, "base": sha})
        self.assertFalse(reply["changed"])
        status, _ = self.server.request("POST", "/api/creatures", {"data": doc, "base": "0" * 16})
        self.assertEqual(status, 409)
        status, reply = self.server.request("POST", "/api/creatures", {"data": [], "base": sha})
        self.assertEqual(status, 400, reply)

    def test_palette_stale_base_is_refused(self):
        palette = self.server.get("/api/data")["palette"]
        status, reply = self.server.request("POST", "/api/palette", {"index": 0, "color": "#010203", "base": "#fefefe"})
        self.assertEqual(status, 409, reply)
        self.assertEqual(self.server.get("/api/data")["palette"], palette)

    def test_lint_waiver_round_trip_stale_and_invalid(self):
        data = self.server.get("/api/data")
        self.assertIn("max_colours", data["lint"])
        self.assertIsNone(data["lint_error"])
        key, path = data["assets"][0]["key"], self.slice / "source/lint.json"
        reply = self.server.post("/api/lint-waiver", {"key": key, "rules": ["specks"], "reason": " intentional ",
                                                     "base": data["lint_sha"], "artist": "e2e"})
        self.assertEqual(reply["lint"]["waivers"][key], {"rules": ["specks"], "reason": "intentional"})
        self.assertEqual(json.loads(path.read_text())["waivers"][key]["reason"], "intentional")
        self.assertEqual(self.server.get("/api/data")["lint_sha"], reply["lint_sha"])
        status, stale = self.server.request("POST", "/api/lint-waiver", {"key": key, "rules": [], "base": data["lint_sha"]})
        self.assertEqual(status, 409, stale)
        self.assertEqual(stale["current"], reply["lint_sha"])
        for body in ({"key": key, "rules": ["nope"], "reason": "x"}, {"key": key, "rules": ["specks"], "reason": " "},
                     {"key": "icons.missing", "rules": ["specks"], "reason": "x"}, {"key": key, "rules": "specks"},
                     {"key": key, "rules": ["specks", "specks"], "reason": "x"}):
            status, error = self.server.request("POST", "/api/lint-waiver", body)
            self.assertEqual(status, 400, (body, error))
        removed = self.server.post("/api/lint-waiver", {"key": key, "rules": [], "base": reply["lint_sha"]})
        self.assertNotIn(key, removed["lint"]["waivers"])
        self.assertNotIn(key, json.loads(path.read_text())["waivers"])

    @unittest.skipUnless(shutil.which("node"), "node runs lint.js")
    def test_page_and_server_lint_agree_on_every_asset(self):
        sys.path.insert(0, str(REPO / "tools/assets"))
        import lint_rules
        data = self.server.get("/api/data")
        configs = [data["lint"], {**data["lint"], "max_colours": 3, "max_colours_by_kind": {"creatures": None},
                                  "waivers": {a["key"]: {"rules": ["specks", "colours"], "reason": "test"}
                                              for a in data["assets"][::3]}}]
        cases = [{"key": a["key"], "kind": a["kind"], "metrics": a["after_metrics"]} for a in data["assets"]]
        script = ("const L = require(process.argv.at(-1)), {cases, configs} = JSON.parse(require('fs').readFileSync(0, 'utf8'));"
                  "console.log(JSON.stringify(configs.map(c => cases.map(k => L.verdict(k.key, k.kind, k.metrics, c)))));")
        result = subprocess.run(["node", "-e", script, "--", str(HERE / "lint.js")],
                                input=json.dumps({"cases": cases, "configs": configs}), capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        page = json.loads(result.stdout)
        server = [[lint_rules.verdict(k["key"], k["kind"], k["metrics"], c) for k in cases] for c in configs]
        self.assertEqual(page, server)
        self.assertTrue(any(v["waived"] for v in server[1]) and any(v["fails"] for v in server[0]))

    @unittest.skipUnless(shutil.which("node"), "node runs lint.js")
    def test_page_and_server_measure_agree_on_every_png(self):
        """compare_slice.measure() and lint.js measure() find the same specks, edges and colours, own palettes included."""
        sys.path.insert(0, str(REPO / "tools/assets"))
        import compare_slice
        manifest = json.loads((self.slice / "assets.json").read_text())
        details = {a["key"]: a for a in self.server.get("/api/data")["assets"]}
        cases, server = [], []
        for asset in manifest["assets"]:
            image = Image.open(self.slice / asset["path"]).convert("RGBA")
            detail = details[asset["key"]]
            own = detail["palette"] if detail["palette_name"] != "shared" else None
            cases.append({"w": image.width, "h": image.height, "kind": asset["kind"], "palette": own,
                          "rgba": base64.b64encode(image.tobytes()).decode()})
            server.append(compare_slice.measure_image(image, asset["kind"], compare_slice.outline_ink(manifest, asset)))
        self.assertTrue(any(c["palette"] and c["kind"] == "creatures" for c in cases))  # the axolotl's own palette
        script = ("const L = require(process.argv.at(-1)), cases = JSON.parse(require('fs').readFileSync(0, 'utf8'));"
                  "console.log(JSON.stringify(cases.map(c => L.measure({w: c.w, h: c.h, data: Buffer.from(c.rgba, 'base64')},"
                  " c.kind, L.outlineInk(c.palette)))));")
        result = subprocess.run(["node", "-e", script, "--", str(HERE / "lint.js")], input=json.dumps(cases),
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        for asset, page, ours in zip(manifest["assets"], json.loads(result.stdout), server):
            self.assertEqual(page, ours, asset["key"])

    def test_waivers_refuse_a_symlinked_or_invalid_lint_file(self):
        path = self.slice / "source/lint.json"
        original = path.read_text()
        elsewhere = Path(self.scratch.name) / "elsewhere.json"
        try:
            elsewhere.write_text(original)
            path.unlink()
            path.symlink_to(elsewhere)
            key = self.data["assets"][0]["key"]
            status, reply = self.server.request("POST", "/api/lint-waiver", {"key": key, "rules": ["specks"], "reason": "x"})
            self.assertEqual(status, 400, reply)
            self.assertEqual(elsewhere.read_text(), original)  # the link's target is untouched
            path.unlink()
            path.write_text('{"max_colours": 6, "max_colours_by_kind": {"sprites": 4}}')
            data = self.server.get("/api/data")  # the page still loads, and says why there are no limits
            self.assertIn("unknown kinds", data["lint_error"])
            status, reply = self.server.request("POST", "/api/lint-waiver", {"key": key, "rules": ["specks"], "reason": "x"})
            self.assertEqual(status, 400, reply)
            self.assertIn("Fix lint.json", reply["error"])
            path.unlink()
            path.mkdir()  # unreadable as a file: still a report, not a 500
            self.assertIn("lint.json", self.server.get("/api/data")["lint_error"])
            path.rmdir()
        finally:
            if path.is_symlink() or path.is_file():
                path.unlink()
            elif path.is_dir():
                path.rmdir()
            path.write_text(original)

    def test_health_reports_capabilities(self):
        health = self.server.get("/healthz")
        self.assertTrue(health["ok"])
        self.assertEqual(health["problems"], [])
        self.assertEqual(health["capabilities"]["cmake"], HAS_CMAKE)


DRAFTS_CHECK = r"""
const fs = require('fs'), vm = require('vm');
const store = new Map(); let quota = Infinity, timers = [];
const localStorage = {get length() { return store.size; }, key: i => [...store.keys()][i] ?? null,
  getItem: k => store.has(k) ? store.get(k) : null, removeItem: k => store.delete(k),
  setItem(k, v) { const used = [...store.values()].reduce((n, s) => n + s.length, 0); if (used + v.length > quota) throw new Error('QuotaExceededError'); store.set(k, String(v)); }};
const window = {localStorage, addEventListener: (name, fn) => { window[name] = fn; }};
const ctx = {window, setTimeout: fn => { timers.push(fn); return timers.length; }, clearTimeout: () => {}, btoa: s => Buffer.from(s, 'binary').toString('base64'),
  atob: s => Buffer.from(s, 'base64').toString('binary'), Uint8ClampedArray, Date, JSON, String, Object, Math};
vm.runInNewContext(fs.readFileSync(process.argv.at(-1), 'utf8'), ctx);
const D = window.JelliDrafts, out = {};
D.put('paint', 'a', 'sha1', 'data1');
out.fresh = D.restore('paint', 'a', 'sha1');
out.stale = D.restore('paint', 'a', 'sha2').stale;
out.list = D.list('paint').map(d => d.key);
const bytes = new Uint8ClampedArray([0, 1, 254, 255]);
out.bytes = Array.from(D.decodeBytes(D.encodeBytes(bytes)));
D.later('paint', 'b', 'x', () => 'late'); out.beforeFlush = D.get('paint', 'b'); window.pagehide(); out.afterFlush = D.get('paint', 'b').data;
D.drop('paint', 'a'); out.dropped = D.get('paint', 'a');
quota = 400; for (let i = 0; i < 10; i++) D.put('paint', 'k' + i, 's', 'x'.repeat(60));
out.newestKept = !!D.get('paint', 'k9');
console.log(JSON.stringify(out));
"""


@unittest.skipUnless(shutil.which("node"), "node runs drafts.js")
class DraftsTest(unittest.TestCase):
    def test_drafts_api(self):
        result = subprocess.run(["node", "-e", DRAFTS_CHECK, "--", str(HERE / "drafts.js")], capture_output=True,
                                text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual(out["fresh"]["data"], "data1")
        self.assertFalse(out["fresh"]["stale"])
        self.assertTrue(out["stale"])
        self.assertEqual(out["list"], ["a"])
        self.assertEqual(out["bytes"], [0, 1, 254, 255])
        self.assertIsNone(out["beforeFlush"])
        self.assertEqual(out["afterFlush"], "late")
        self.assertIsNone(out["dropped"])
        self.assertTrue(out["newestKept"])  # a full store evicts the oldest drafts instead of failing


class StartupTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="jelli-art-start-")
        self.slice, self.content = copy_tree(Path(self.scratch.name))
        self.addCleanup(self.scratch.cleanup)

    def start(self, env=None):
        server = Server(["--assets", str(self.slice), "--content", str(self.content)], env=env)
        self.addCleanup(server.stop)
        return server

    def test_orphaned_temp_files_are_removed(self):
        orphans = [self.slice / ".assets.json.jelli-art-tmp-abc123", self.slice / "assets.json.tmp",
                   self.content / ".behaviors.json.jelli-art-tmp-x", self.slice / "creatures/.baby.png.jelli-art-tmp-1"]
        for orphan in orphans:
            orphan.parent.mkdir(parents=True, exist_ok=True)
            orphan.write_text("{half")
        server = self.start()
        self.assertEqual([p for p in orphans if p.exists()], [])
        self.assertEqual(len(server.get("/api/data")["startup"]["removed_temp_files"]), len(orphans))

    def test_half_written_json_is_reported_not_fatal(self):
        (self.slice / "source/hand-painted.json").write_text('["creatures.baby')
        server = self.start()
        problems = server.get("/healthz")["problems"]
        self.assertTrue(any("hand-painted.json" in p for p in problems), problems)

    def test_missing_cmake_makes_behaviour_read_only_with_a_reason(self):
        bin_dir = Path(self.scratch.name) / "bin"
        bin_dir.mkdir()
        for tool in ("git",):
            if shutil.which(tool):
                (bin_dir / tool).symlink_to(shutil.which(tool))
        server = self.start(env={"PATH": str(bin_dir)})
        data = server.get("/api/behaviour")
        self.assertFalse(data["behavior_editable"])
        self.assertIn("cmake", data["behavior_readonly_reason"])
        status, reply = server.request("POST", "/api/content", {"docs": {"behaviors": data["behavior_data"]},
                                                                 "bases": {"behaviors": data["behavior_data_sha"]}})
        self.assertEqual(status, 200, reply)  # unchanged: nothing to validate
        doc = dict(data["behavior_data"], extra_field_for_test=1)
        status, reply = server.request("POST", "/api/content", {"docs": {"behaviors": doc},
                                                                 "bases": {"behaviors": data["behavior_data_sha"]}})
        self.assertEqual(status, 400, reply)
        self.assertIn("cmake is not installed", reply["error"])
        caps = server.get("/healthz")["capabilities"]
        self.assertFalse(caps["cmake"])
        self.assertFalse(caps["node"])
        server.get("/api/data")  # the rest of the studio still works


@unittest.skipUnless(HAS_GIT, "git commit tests need git")
class GitTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="jelli-art-git-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.work, self.origin = make_checkout(self.root)

    def start(self, push=False):
        args = ["--git-branch", "art/studio", "--git-base", "main"] + (["--git-push"] if push else [])
        server = Server(args, env={"JELLI_REPO": str(self.work)})
        self.addCleanup(server.stop)
        return server

    def paint(self, server, skip=()):
        data = server.get("/api/data")
        asset = first_editable(data, skip=skip)
        path = self.work / "assets/slice" / next(a["path"] for a in json.loads(
            (self.work / "assets/slice/assets.json").read_text())["assets"] if a["key"] == asset["key"])
        reply = server.post("/api/save", {"key": asset["key"], "pixels": repaint(pixels_of(path)), "base": asset["sha"],
                                          "artist": "Ada"})
        return asset["key"], reply

    def test_save_commits_only_its_files_with_trailer(self):
        server = self.start()
        (self.work / "unrelated.txt").write_text("staged by someone else\n")
        git(self.work, "add", "unrelated.txt")
        key, reply = self.paint(server)
        self.assertTrue(reply.get("commit"), reply)
        message = git(self.work, "log", "-1", "--format=%B")
        self.assertIn(f"repaint {key}", message)
        self.assertIn("Painted-by: Ada", message)
        files = git(self.work, "show", "--name-only", "--format=", "HEAD").split("\n")
        self.assertNotIn("unrelated.txt", files)
        self.assertTrue(any(f.endswith(".png") for f in files), files)
        self.assertIn("unrelated.txt", git(self.work, "diff", "--cached", "--name-only"))  # still staged, untouched

    def test_failed_commit_is_queued_and_retried(self):
        server = self.start()
        lock = self.work / ".git/index.lock"
        lock.write_text("")
        key, reply = self.paint(server)
        self.assertIn("git_error", reply)
        self.assertTrue(reply.get("git_queued"))
        status = server.get("/api/git")
        self.assertEqual(status["pending_commits"], 1)
        self.assertTrue(any("queued" in p for p in server.get("/healthz")["problems"]))
        lock.unlink()
        server.post("/api/git/retry", {})
        wait_for(lambda: server.get("/api/git")["pending_commits"] == 0, message="queued commit")
        self.assertIn(f"repaint {key}", git(self.work, "log", "-1", "--format=%s"))
        self.assertEqual(git(self.work, "status", "--porcelain", "--", "assets/slice"), "")

    def test_uncommitted_studio_edits_are_committed_on_start(self):
        manifest = self.work / "assets/slice/assets.json"
        manifest.write_text(manifest.read_text().replace('"palette"', '"palette"', 1) + "\n")
        self.start()
        self.assertIn("recover", git(self.work, "log", "-1", "--format=%s"))
        self.assertEqual(git(self.work, "status", "--porcelain", "--", "assets/slice", "content"), "")

    def test_push_reaches_origin(self):
        server = self.start(push=True)
        self.paint(server)
        wait_for(lambda: server.get("/api/git")["last_push"]["ok"], message="push")
        self.assertEqual(git(self.origin, "rev-parse", "refs/heads/art/studio"), git(self.work, "rev-parse", "HEAD"))
        self.assertEqual(server.get("/api/git")["unpushed"], 0)

    def test_push_failure_is_visible_and_retried(self):
        git(self.work, "remote", "set-url", "origin", str(self.root / "missing.git"))
        server = self.start(push=True)
        self.paint(server)
        status = wait_for(lambda: (s := server.get("/api/git"))["last_push"]["ok"] is False and s, message="push failure")
        self.assertTrue(status["next_retry_at"])
        self.assertTrue(any("push failing" in p for p in server.get("/healthz")["problems"]))
        git(self.work, "remote", "set-url", "origin", str(self.origin))
        server.post("/api/git/retry", {})
        wait_for(lambda: server.get("/api/git")["last_push"]["ok"], message="push after retry")

    def other_clone(self):
        other = self.root / "other"
        git(self.root, "clone", "--quiet", str(self.origin), str(other))
        return other

    def test_push_rebases_onto_new_remote_commits(self):
        server = self.start(push=True)
        first, _ = self.paint(server)
        wait_for(lambda: server.get("/api/git")["last_push"]["ok"], message="first push")
        other = self.other_clone()
        git(other, "checkout", "--quiet", "art/studio")
        (other / "notes.txt").write_text("a reviewer's note\n")
        git(other, "add", "notes.txt")
        git(other, "commit", "--quiet", "-m", "docs: note")
        git(other, "push", "--quiet", "origin", "art/studio")
        self.paint(server, skip={first})
        wait_for(lambda: git(self.origin, "rev-parse", "refs/heads/art/studio") == git(self.work, "rev-parse", "HEAD"),
                 message="rebased push")
        self.assertIn("notes.txt", git(self.work, "ls-files"))

    def test_push_replaces_a_remote_branch_whose_art_landed(self):
        server = self.start(push=True)
        first, _ = self.paint(server)
        wait_for(lambda: server.get("/api/git")["last_push"]["ok"], message="first push")
        server.stop()
        other = self.other_clone()  # squash-merge the studio branch into main
        git(other, "checkout", "--quiet", "main")
        git(other, "checkout", "origin/art/studio", "--", "assets/slice", "content")
        git(other, "commit", "--quiet", "-m", "feat: studio art (#1)")
        git(other, "push", "--quiet", "origin", "main")
        git(self.work, "fetch", "--quiet", "origin")
        git(self.work, "reset", "--quiet", "--hard", "origin/main")  # what entrypoint.sh does on start
        server = self.start(push=True)
        self.paint(server, skip={first})
        wait_for(lambda: git(self.origin, "rev-parse", "refs/heads/art/studio") == git(self.work, "rev-parse", "HEAD"),
                 message="force-with-lease push")

    def start_entrypoint(self):
        env = {"JELLI_REPO": str(self.work), "JELLI_STUDIO_BRANCH": "art/studio", "JELLI_APP": str(HERE),
               "JELLI_HOST": "127.0.0.1",
               "GIT_CONFIG_GLOBAL": str(self.root / "gitconfig"),
               "PATH": f"{Path(sys.executable).parent}{os.pathsep}{os.environ['PATH']}"}
        server = Server([], env=env, command=["sh", str(HERE / "entrypoint.sh")])
        self.addCleanup(server.stop)
        return server

    @unittest.skipIf(os.name == "nt", "entrypoint.sh is the container's POSIX start script")
    def test_entrypoint_keeps_unlanded_content_and_starts_offline(self):
        content = self.work / "content/creatures.json"
        content.write_text(json.dumps(json.loads(content.read_text()), indent=2) + "\n\n")
        git(self.work, "commit", "--quiet", "-am", "chore(art): content-only studio commit")
        studio_head = git(self.work, "rev-parse", "HEAD")
        git(self.work, "remote", "set-url", "origin", str(self.root / "offline.git"))  # fetch fails
        (self.work / ".git/index.lock").write_text("")  # left by a killed git
        server = self.start_entrypoint()
        log = git(self.work, "log", "--format=%h %s", "-5")
        self.assertEqual(git(self.work, "rev-parse", "HEAD"), studio_head, log)
        self.assertFalse((self.work / ".git/index.lock").exists())
        self.assertTrue(server.get("/api/git")["enabled"])

    @unittest.skipIf(os.name == "nt", "entrypoint.sh is the container's POSIX start script")
    def test_entrypoint_follows_new_base_art_when_studio_has_none(self):
        other = self.other_clone()  # main gains art the idle studio branch never saw
        git(other, "checkout", "--quiet", "main")
        content = other / "content/creatures.json"
        content.write_text(json.dumps(json.loads(content.read_text()), indent=2) + "\n\n")
        git(other, "commit", "--quiet", "-am", "feat: new species")
        git(other, "push", "--quiet", "origin", "main")
        server = self.start_entrypoint()
        self.assertEqual(git(self.work, "rev-parse", "HEAD"), git(self.origin, "rev-parse", "refs/heads/main"))
        self.assertTrue(server.get("/api/git")["enabled"])


if __name__ == "__main__":
    # Under ctest's 300 s limit, print every thread's stack first so a stall shows where it is.
    faulthandler.dump_traceback_later(270, exit=True)
    unittest.main()
