# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Jelli Art, the pixel-art studio: the before/after review page plus painting and palette editing.

Listens on 127.0.0.1 by default (the container passes --host 0.0.0.0 behind its
port mapping). Every write goes to a PNG named by the manifest, so the studio can
never touch an arbitrary path. Saved art is recorded in
assets/slice/source/hand-painted.json, which the polish recipe leaves alone.
Creature clip edits change only the "clips" entries of assets.json and are
validated by the checkout's tools/assets/build_slice.py before they are written.
Creature render profiles and state looks are written to content/creatures.json
after the checkout's tools/assets/creature_data.py accepts them, and behaviour
states and repertoires to content/behaviors.json after the checkout's
cmake/JelliBehaviors.cmake accepts them (cmake -P on a scratch copy).
With --git-branch, each save is also committed (and with --git-push, pushed).
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import socketserver
import sys
import threading
import traceback
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "assets"))
from compare_slice import REPO, SOURCE, collect  # noqa: E402
import animation  # noqa: E402
import behaviors  # noqa: E402
import creatures  # noqa: E402
import lint_rules  # noqa: E402
import game_preview  # noqa: E402
import profiles  # noqa: E402
import request_body  # noqa: E402
import storage  # noqa: E402
from git_sync import GitSync  # noqa: E402

# Art recipes come from the served checkout so tidy matches that checkout's palette.
sys.path.insert(0, str(SOURCE / "source"))
try:
    from icon_polish import reink  # noqa: E402
    from pixel_kit import to_image  # noqa: E402
except ImportError:  # an older checkout without the polish recipe: tidy is unavailable
    reink = to_image = None

MANIFEST = SOURCE / "assets.json"
HAND_PAINTED = SOURCE / "source/hand-painted.json"
LINT = SOURCE / lint_rules.RELATIVE  # limits and waivers (lint_rules.py)
CONTENT = REPO / "content"
PETS, CREATURE_DATA = CONTENT / "pets.json", CONTENT / "creatures.json"
CONTENT_WRITABLE = True  # False for --assets trials without --content, so a trial never edits the checkout
TEMPLATE = HERE.parent / "assets/compare.html"
STUDIO_JS = HERE / "studio.js"
CREATURE_JS = HERE / "creature.js"
BEHAVIOUR_JS = HERE / "behaviour.js"
SHELL_JS = HERE / "shell.js"  # first: the page frame and window.JelliShell, which later scripts use
PAGE_SCRIPTS = (SHELL_JS, HERE / "lint.js", HERE / "paint_tools.js", STUDIO_JS, CREATURE_JS, HERE / "animation.js", HERE / "flipbook.js", BEHAVIOUR_JS, HERE / "simulator.js",
                HERE / "reactions.js", HERE / "game_preview.js", HERE / "lint_ui.js")
EDITABLE_CONTENT = ("behaviors", "creatures")
STUDIO_VERSION = (HERE / "VERSION").read_text().strip()
GIT = None  # GitSync when committing saves
MAX_BODY = 1 << 20
# The drafts API (window.JelliDrafts); pages call it only after boot, so its place in the list does not matter.
PAGE_SCRIPTS += (HERE / "drafts.js",)
HEX_COLOUR = re.compile(r"#[0-9a-fA-F]{6}")
LOCK = storage.WriteLock()  # every save holds it: one writer per served root, across studio processes
STARTUP = {"removed_temp_files": [], "problems": []}


class StudioError(ValueError):
    """A request the studio refuses, reported to the page as a 400."""

    status = HTTPStatus.BAD_REQUEST


class StaleError(StudioError):
    """The file changed since the page loaded it (409); the page should reload before saving."""

    status = HTTPStatus.CONFLICT

    def __init__(self, message, current=None):
        super().__init__(message)
        self.current = current  # the base now on disk, so a page can overwrite after asking the artist


def read_manifest():
    return json.loads(MANIFEST.read_text())


def write_json(path, value):
    """Atomic replace so the live game watcher never sees a half-written file."""
    storage.write_file(path, storage.json_bytes(value))


def asset_path(asset):
    """The asset's PNG, confined to the served assets directory."""
    try:
        return storage.confined(SOURCE, asset["path"])
    except storage.UnsafePath as error:
        raise StudioError(str(error)) from error


def clip_digest(manifest, key):
    """Hash of one clip entry, or None when the form has no such clip; pages send it back as the clip's base."""
    clip = next((c for c in manifest.get("clips", []) if c.get("key") == key), None)
    return None if clip is None else storage.digest_bytes(json.dumps(clip, sort_keys=True).encode())


def capabilities():
    """Which optional tools this host has, so the page can say why an editor is read-only."""
    return {"cmake": shutil.which("cmake") is not None, "git": shutil.which("git") is not None,
            "node": shutil.which("node") is not None, "tidy": reink is not None,
            "content_writable": CONTENT_WRITABLE}


def version():
    """Changes whenever the manifest, creature content or any PNG changes on disk."""
    digest = hashlib.sha1()
    content = [PETS, CREATURE_DATA, *(CONTENT / f"{n}.json" for n in ("behaviors", "activities", "potty"))]
    for path in [MANIFEST, *[p for p in [*content, LINT] if p.exists()], *sorted(SOURCE.glob("*/*.png"))]:
        stat = path.stat()
        digest.update(f"{path.name}{stat.st_mtime_ns}{stat.st_size}".encode())
    return digest.hexdigest()[:16]


def hand_painted():
    return json.loads(HAND_PAINTED.read_text()) if HAND_PAINTED.exists() else []


def payload(before):
    manifest, records = collect(before, SOURCE)
    painted = set(hand_painted())
    details = creatures.asset_details(manifest)
    paths = {a["key"]: a["path"] for a in manifest["assets"]}
    for record in records:
        record["hand_painted"] = record["key"] in painted
        record.update(details[record["key"]])
        record["sha"] = storage.digest(SOURCE / paths[record["key"]])  # the base a paint save sends back
    return {"live": True, "before_label": before, "after_label": "working tree", "version": version(),
            "studio_version": STUDIO_VERSION, "palette": manifest["palette"], "assets": records,
            **creatures.creature_data(manifest, PETS), **creature_profiles(), **behaviour_data(),
            "clip_shas": {c["key"]: clip_digest(manifest, c["key"]) for c in manifest.get("clips", [])},
            **lint_payload(),
            "capabilities": capabilities(), "startup": STARTUP,
            "git": GIT.status() if GIT else {"enabled": False}}


def lint_payload():
    """Lint limits and waivers, the hash a waiver edit sends back, and any problem reading them."""
    lint, error = lint_rules.load(SOURCE)
    return {"lint": lint, "lint_sha": storage.digest(LINT), "lint_error": error}


def set_waiver(key, rules, reason, artist="", base=None):
    """Add, replace or (with no rules) remove one sprite's lint waiver in source/lint.json."""
    with LOCK:
        current = storage.digest(LINT)
        if base is not None and base != current:
            raise StaleError("Lint waivers changed on disk since you loaded them; reload first", current)
        lint, error = lint_rules.load(SOURCE)
        if error:
            raise StudioError(f"Fix {LINT.name} before changing waivers: {error}")
        keys = {a["key"] for a in read_manifest()["assets"]}
        try:
            updated = lint_rules.set_waiver(lint, key, rules, reason, keys)
        except lint_rules.LintError as failure:
            raise StudioError(str(failure)) from failure
        data = storage.json_bytes(updated)
        storage.write_file(storage.confined(SOURCE, lint_rules.RELATIVE), data)
        action = "waive" if rules else "clear waiver for"
        git = record([LINT], f"chore(art): {action} lint on {key} in Jelli Art", artist)
    return {"ok": True, "lint": updated, "lint_sha": storage.digest_bytes(data), "version": version(), **git}


def content_paths():
    """The content files the studio may write, by name."""
    return {"behaviors": CONTENT / "behaviors.json", "creatures": CREATURE_DATA}


def behaviour_data():
    path = content_paths()["behaviors"]
    vocab = behaviors.vocabulary(REPO, CONTENT)
    reasons = [why for missing, why in (
        (not CONTENT_WRITABLE, "this trial serves copied art without --content"),
        (vocab is None, "the checkout has no cmake/JelliBehaviors.cmake"),
        (not path.exists(), "content/behaviors.json is missing"),
        (shutil.which("cmake") is None, "cmake is not installed, so edits cannot be validated")) if missing]
    return {"behavior_data": behaviors.read_json(path), "behavior_data_sha": profiles.digest(path),
            "behavior_vocab": vocab, "behavior_editable": not reasons,
            "behavior_readonly_reason": "; ".join(reasons),
            "potty": behaviors.read_json(CONTENT / "potty.json")}


def creature_profiles():
    data = profiles.profile_data(CREATURE_DATA, creatures.load_checkout_module(REPO, "creature_data"))
    data["creature_data_editable"] = data["creature_data_editable"] and CONTENT_WRITABLE
    return data


def refs():
    def git(*args):
        try:
            result = subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True, check=False)
        except OSError:  # git is not installed
            return []
        return result.stdout.split("\n") if result.returncode == 0 else []
    commits = [line.split(" ", 1) for line in git("log", "-12", "--format=%h %s") if line]
    return {"tags": [t for t in git("tag", "--sort=-creatordate") if t][:8],
            "commits": [{"ref": c[0], "subject": c[1] if len(c) > 1 else ""} for c in commits]}


def find_asset(manifest, key):
    for asset in manifest["assets"]:
        if asset["key"] == key:
            return asset
    raise StudioError(f"Unknown asset: {key}")


def image_from_pixels(asset, pixels):
    """Rows of '#rrggbb' or null, validated against the manifest size, to an RGBA image."""
    w, h = asset["width"], asset["height"]
    if not isinstance(pixels, list) or len(pixels) != w * h:
        raise StudioError(f"Expected a list of {w * h} pixels for {asset['key']}")
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for i, color in enumerate(pixels):
        if color is None:
            continue
        if not (isinstance(color, str) and HEX_COLOUR.fullmatch(color)):
            raise StudioError(f"Pixel {i} must be '#rrggbb' or null")
        image.putpixel((i % w, i // w), tuple(bytes.fromhex(color[1:])) + (255,))
    if image.getchannel("A").getbbox() is None:
        raise StudioError("An asset needs at least one opaque pixel")
    return image


def pixels_from_image(image):
    return [f"#{r:02x}{g:02x}{b:02x}" if a else None for r, g, b, a in image.convert("RGBA").getdata()]


def record(paths, subject, artist):
    """Commit the written files when git sync is on; a git failure never loses the save."""
    if not GIT:
        return {}
    try:
        sha = GIT.commit([p.relative_to(REPO) for p in paths], subject, artist)
        return {"commit": sha}
    except RuntimeError as error:
        return {"git_error": str(error), "git_queued": True}


def save_asset(key, pixels, artist="", base=None):
    """Write one sprite. base is the PNG hash the page loaded (record["sha"]); a mismatch is refused."""
    with LOCK:
        manifest = read_manifest()
        asset = find_asset(manifest, key)
        if asset["kind"] in ("font", "backgrounds"):
            raise StudioError("Font and backgrounds are not editable in the studio yet")
        path = asset_path(asset)
        current = storage.digest(path)
        if base is not None and base != current:
            raise StaleError(f"{key} changed on disk since you loaded it; reload to see the new version", current)
        image = image_from_pixels(asset, pixels)
        data = storage.png_bytes(image)
        asset["bounds"] = list(image.getchannel("A").getbbox())
        files = [(path, data), (MANIFEST, storage.json_bytes(manifest))]
        painted = hand_painted()
        if key not in painted:
            files.append((HAND_PAINTED, storage.json_bytes(sorted([*painted, key]))))
        storage.write_files(files)
        git = record([path, MANIFEST, HAND_PAINTED], f"chore(art): repaint {key} in Jelli Art", artist)
    return {"ok": True, "version": version(), "sha": storage.digest_bytes(data), **git}


def tidy(key, pixels):
    """The polish re-ink pass: closed 1px outline, no specks, bottom shadow on 32px art."""
    if reink is None:
        raise StudioError("This checkout has no polish recipe, so Tidy is unavailable")
    asset = find_asset(read_manifest(), key)
    if "palette" in asset:
        raise StudioError("Tidy inks with the shared palette; this sprite has its own palette")
    sprite = reink(image_from_pixels(asset, pixels))
    return {"pixels": pixels_from_image(to_image(sprite, (asset["width"], asset["height"])))}


def save_clips(edits, artist="", bases=None):
    """Change frames, durations and loop of existing creature clips; nothing else in the manifest moves.

    bases maps a clip key to the hash the page loaded (payload "clip_shas", null for a
    clip that did not exist); a clip whose entry changed since is refused. Keys left
    out are not checked, for pages that predate bases.
    """
    if bases is not None and not isinstance(bases, dict):
        raise StudioError("bases must map clip keys to the hashes the page loaded")
    with LOCK:
        original = read_manifest()
        for key, base in (bases or {}).items():
            if base != clip_digest(original, key):
                raise StaleError(f"Clip {key} changed since you loaded it; reload to see the new version",
                                 {k: clip_digest(original, k) for k in bases})
        manifest = json.loads(json.dumps(original))
        validator = creatures.load_validator(REPO)
        cap = getattr(validator, "CLIP_FRAME_CAP", creatures.FRAME_CAP)
        try:
            changed = creatures.apply_edits(manifest, edits, cap)
            if not changed:
                return {"ok": True, "changed": [], "version": version()}
            warning = creatures.validate(manifest, original, SOURCE, validator)
        except creatures.ClipError as error:
            raise StudioError(str(error)) from error
        write_json(MANIFEST, manifest)
        names = ", ".join(changed) if len(changed) <= 3 else f"{len(changed)} creature"
        git = record([MANIFEST], f"chore(art): edit {names} clips in Jelli Art", artist)
    shas = {key: clip_digest(manifest, key) for key in changed}
    return {"ok": True, "changed": changed, "version": version(), "clip_shas": shas,
            **({"warning": warning} if warning else {}), **git}


def frames_overview():
    return {"frames": animation.frame_overview(read_manifest(), SOURCE, PETS, CREATURE_DATA)}


def edit_frame(body, artist=""):
    """Add (blank or duplicated) or retire a creature frame; see animation.py."""
    action = body.get("action")
    with LOCK:
        try:
            if action == "add":
                result, paths, subject = animation.add_frame(SOURCE, REPO, body)
            elif action == "retire":
                result, paths, subject = animation.retire_frame(SOURCE, REPO, str(body.get("key")), PETS, CREATURE_DATA)
            else:
                raise StudioError("Frame action must be add or retire")
        except animation.FrameError as error:
            raise StudioError(str(error)) from error
        git = record(paths, subject, artist)
    return {"ok": True, **result, "version": version(), **git}


def save_content(docs, bases, artist=""):
    """Replace content/behaviors.json and/or content/creatures.json together.

    bases holds the hash of each file as the page loaded it, so a save never
    overwrites changes made elsewhere. The candidates are validated as a set:
    behaviors.json by cmake/JelliBehaviors.cmake, creatures.json (profiles and
    one look per behaviour state) by creature_data.py, then written in one commit.
    """
    if not isinstance(docs, dict) or not docs or not set(docs) <= set(EDITABLE_CONTENT):
        raise StudioError(f"Save one or more of: {', '.join(EDITABLE_CONTENT)}")
    bases = bases if isinstance(bases, dict) else {}
    with LOCK:
        if not CONTENT_WRITABLE:
            raise StudioError("This trial serves copied assets; pass --content with a copy of content/ to edit creature data")
        paths = content_paths()
        for name in docs:
            if bases.get(name) != profiles.digest(paths[name]):
                raise StaleError(f"content/{name}.json changed since you loaded it; revert to load the new version",
                                 {n: profiles.digest(p) for n, p in paths.items()})
        current = {name: behaviors.read_json(path) for name, path in paths.items()}
        changed = {name: doc for name, doc in docs.items() if doc != current[name]}
        git = {}
        if changed:
            candidate = {**current, **changed}
            try:
                if "behaviors" in changed:
                    behaviors.validate(candidate["behaviors"], REPO, CONTENT)
                module = creatures.load_checkout_module(REPO, "creature_data")
                profiles.validate(candidate["creatures"], read_manifest(), module, PETS, candidate["behaviors"])
            except (behaviors.BehaviorError, profiles.ProfileError) as error:
                raise StudioError(str(error)) from error
            storage.write_files([(paths[name], storage.json_bytes(doc)) for name, doc in changed.items()])
            files = " and ".join(f"{name}.json" for name in sorted(changed))
            git = record([paths[name] for name in sorted(changed)], f"chore(art): edit {files} in Jelli Art", artist)
    return {"ok": True, "changed": sorted(changed), "version": version(),
            "shas": {name: profiles.digest(path) for name, path in paths.items()}, **git}


def save_creature_data(data, base, artist=""):
    """content/creatures.json alone (the Creature view's Behaviour & size panel)."""
    result = save_content({"creatures": data}, {"creatures": base}, artist)
    extra = {k: result[k] for k in ("commit", "git_error") if k in result}
    return {"ok": True, "changed": bool(result["changed"]), "version": result["version"],
            "sha": result["shas"]["creatures"], **extra}


def set_palette_slot(index, color, artist="", base=None):
    """Recolour one shared slot everywhere it is used, then update the manifest.

    base is the slot's colour as the page showed it; a slot changed since is refused.
    Every PNG and the manifest are staged first and replaced together.
    """
    if not HEX_COLOUR.fullmatch(color):
        raise StudioError("A palette colour must be '#rrggbb'")
    with LOCK:
        manifest = read_manifest()
        palette = manifest["palette"]
        color = color.lower()
        if not (0 <= index < len(palette)):
            raise StudioError("Unknown palette slot")
        if base is not None and str(base).lower() != palette[index].lower():
            raise StaleError(f"Palette slot {index} is now {palette[index]}; reload before changing it", palette[index])
        if color in palette:
            raise StudioError("Choose a colour that is not already in the palette")
        old = tuple(bytes.fromhex(palette[index][1:]))
        new = tuple(bytes.fromhex(color[1:]))
        changed, files = [], []
        for asset in manifest["assets"]:
            if "palette" in asset:  # backgrounds keep their own grey palette
                continue
            path = asset_path(asset)
            image = Image.open(path).convert("RGBA")
            data = [(new + (255,)) if p[3] and p[:3] == old else p for p in image.getdata()]
            if data != list(image.getdata()):
                image.putdata(data)
                files.append((path, storage.png_bytes(image)))
                changed.append(asset["key"])
        palette[index] = color
        files.append((MANIFEST, storage.json_bytes(manifest)))
        storage.write_files(files)
        paths = [path for path, _ in files]
        git = record(paths, f"chore(art): change palette slot {index} to {color} in Jelli Art", artist)
    return {"ok": True, "changed": changed, "version": version(), **git}


def health():
    """Liveness plus what an operator needs: problems found at startup and the git queue."""
    problems = list(STARTUP["problems"])
    git = GIT.status() if GIT else {"enabled": False}
    if git.get("pending_commits"):
        problems.append(f"{git['pending_commits']} commit(s) queued: {git['last_commit_error']}")
    if git.get("last_push", {}).get("ok") is False:
        problems.append(f"push failing ({git['last_push'].get('kind')}): {git['last_push'].get('error')}")
    return {"ok": True, "studio_version": STUDIO_VERSION, "problems": problems, "capabilities": capabilities(),
            "git": git}


F = request_body.field
POSTS = {  # path -> handler(body); every field is type-checked so a bad request is a clear 400
    "/api/save": lambda b: save_asset(F(b, "key", str), F(b, "pixels", list), request_body.artist(b),
                                      F(b, "base", str, required=False)),
    "/api/tidy": lambda b: tidy(F(b, "key", str), F(b, "pixels", list)),
    "/api/lint-waiver": lambda b: set_waiver(F(b, "key", str), F(b, "rules", list),
                                             F(b, "reason", str, required=False, default=""), request_body.artist(b),
                                             F(b, "base", str, required=False)),
    "/api/clips": lambda b: save_clips(F(b, "clips", list), request_body.artist(b), F(b, "bases", dict, required=False)),
    "/api/creatures": lambda b: save_creature_data(F(b, "data", dict), F(b, "base", str, required=False),
                                                   request_body.artist(b)),
    "/api/content": lambda b: save_content(F(b, "docs", dict), F(b, "bases", dict, required=False, default={}),
                                           request_body.artist(b)),
    "/api/frames": lambda b: edit_frame(b, request_body.artist(b)),  # add or retire (see animation.py)
    "/api/palette": lambda b: set_palette_slot(F(b, "index", int), F(b, "color", str), request_body.artist(b),
                                               F(b, "base", str, required=False)),
}
GETS = {"/", "/api/data", "/healthz", "/api/git", "/api/version", "/api/refs", "/api/clips", "/api/creatures", "/api/frames",
        "/api/behaviour"}


class StudioServer(ThreadingHTTPServer):
    """HTTPServer without its bind-time reverse DNS lookup (socket.getfqdn), which can take
    seconds on some hosts (macOS CI runners) and only fills server_name, which nothing reads."""

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


class Handler(BaseHTTPRequestHandler):
    server_version = "JelliArt/1"

    def send(self, status, body, content_type="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        """The request's JSON object; request_body refuses wrong types, sizes and media types."""
        return request_body.read_json(self.headers, self.rfile, MAX_BODY)

    def route(self, method):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        if method == "GET" and url.path == "/":
            page = TEMPLATE.read_text().replace("__COMPARE_DATA__", json.dumps({"live": True}))
            page = page.replace("/*__STUDIO_JS__*/", "\n".join(p.read_text() for p in PAGE_SCRIPTS))
            return self.send(HTTPStatus.OK, page.encode(), "text/html; charset=utf-8")
        if game_preview.route(self, method, url.path):
            return None
        if method == "GET" and url.path == "/api/data":
            return self.send(HTTPStatus.OK, payload(query.get("before", ["HEAD"])[0]))
        if method == "GET" and url.path == "/healthz":
            return self.send(HTTPStatus.OK, health())
        if method == "GET" and url.path == "/api/git":
            return self.send(HTTPStatus.OK, GIT.status() if GIT else {"enabled": False})
        if method == "POST" and url.path == "/api/git/retry":
            if not GIT:
                raise StudioError("This studio does not commit saves (no --git-branch)")
            GIT.retry_now()
            return self.send(HTTPStatus.OK, GIT.status())
        if method == "GET" and url.path == "/api/version":
            return self.send(HTTPStatus.OK, {"version": version()})
        if method == "GET" and url.path == "/api/refs":
            return self.send(HTTPStatus.OK, refs())
        if method == "GET" and url.path == "/api/clips":
            return self.send(HTTPStatus.OK, creatures.creature_data(read_manifest(), PETS))
        if method == "GET" and url.path == "/api/frames":
            return self.send(HTTPStatus.OK, frames_overview())
        if method == "GET" and url.path == "/api/creatures":
            return self.send(HTTPStatus.OK, creature_profiles())
        if method == "GET" and url.path == "/api/behaviour":
            return self.send(HTTPStatus.OK, {**behaviour_data(), **creature_profiles()})
        if method == "POST" and url.path in POSTS:
            return self.send(HTTPStatus.OK, POSTS[url.path](self.body()))
        if url.path in POSTS or url.path in GETS:
            return self.send(HTTPStatus.METHOD_NOT_ALLOWED, {"error": f"{method} is not supported on {url.path}"})
        return self.send(HTTPStatus.NOT_FOUND, {"error": "Not found"})

    def handle_method(self, method):
        try:
            self.route(method)
        except request_body.RequestError as error:
            self.send(error.status, {"error": str(error)})
        except StudioError as error:
            stale = {"stale": True, "current": error.current} if isinstance(error, StaleError) else {}
            self.send(error.status, {"error": str(error), **stale})
        except ValueError as error:  # ClipError, BehaviorError, an unknown revision: the request is at fault
            self.send(HTTPStatus.BAD_REQUEST, {"error": str(error)})
        except (KeyError, TypeError) as error:  # malformed nested data the validators did not name
            traceback.print_exc()
            self.send(HTTPStatus.BAD_REQUEST, {"error": f"Malformed request: {type(error).__name__} {error}"})
        except storage.Busy as error:
            self.send(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error), "retry": True})
        except (BrokenPipeError, ConnectionResetError):
            pass  # the page went away mid-response
        except Exception as error:  # noqa: BLE001 (answer, log, keep serving)
            traceback.print_exc()
            self.send(HTTPStatus.INTERNAL_SERVER_ERROR,
                      {"error": f"Jelli Art hit an internal error ({type(error).__name__}: {error}). "
                                "Reload to see what is on disk; the server log has details."})

    def do_GET(self):  # noqa: N802 (http.server naming)
        self.handle_method("GET")

    def do_POST(self):  # noqa: N802
        self.handle_method("POST")

    def do_PUT(self):  # noqa: N802
        self.handle_method("PUT")

    def do_DELETE(self):  # noqa: N802
        self.handle_method("DELETE")

    def log_message(self, fmt, *args):
        if "/api/version" not in self.path:
            sys.stderr.write(f"  {self.command} {self.path.split('?')[0]} {args[1] if len(args) > 1 else ''}\n")


def prepare():
    """Crash-safe start: bind the write lock, delete temp files a crash left, report unreadable JSON."""
    LOCK.bind(SOURCE)
    roots = [SOURCE] + ([CONTENT] if CONTENT_WRITABLE else [])
    with LOCK:  # another studio on this root may be mid-save; its temp files are not orphans
        removed = storage.clean_orphans(*roots)
    STARTUP["removed_temp_files"] = [str(p) for p in removed]
    for path in removed:
        print(f"Removed a temporary file an interrupted save left: {path}", flush=True)
    content = [CONTENT / f"{name}.json" for name in ("pets", "creatures", "behaviors", "activities", "potty")]
    problems = storage.check_json(MANIFEST, HAND_PAINTED, LINT, *content)
    STARTUP["problems"] = [f"{path} is not valid JSON ({error}); restore it from git" for path, error in problems]
    for problem in STARTUP["problems"]:
        print(f"warning: {problem}", file=sys.stderr, flush=True)
    if not shutil.which("cmake"):
        print("note: cmake is not installed; the Behaviour view is read-only", file=sys.stderr, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="bind address (the container uses 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true", help="do not open a browser tab")
    parser.add_argument("--assets", type=Path, help="serve a copy laid out like assets/slice (for trials)")
    parser.add_argument("--content", type=Path, help="read and write a copy laid out like content/ (for trials)")
    parser.add_argument("--git-branch", help="commit every save to this branch of the checkout")
    parser.add_argument("--git-push", action="store_true", help="push --git-branch to origin after commits")
    parser.add_argument("--git-base", default="main",
                        help="branch studio work lands on; a rejected push whose remote art is already there is replaced")
    args = parser.parse_args()
    if args.assets:
        if args.git_branch:
            parser.error("--assets trials cannot be combined with --git-branch")
        global SOURCE, MANIFEST, HAND_PAINTED, LINT
        SOURCE = args.assets.resolve()
        MANIFEST, HAND_PAINTED = SOURCE / "assets.json", SOURCE / "source/hand-painted.json"
        LINT = SOURCE / lint_rules.RELATIVE
    global CONTENT, PETS, CREATURE_DATA, CONTENT_WRITABLE
    if args.content:
        if args.git_branch:
            parser.error("--content trials cannot be combined with --git-branch")
        CONTENT = args.content.resolve()
        PETS, CREATURE_DATA = CONTENT / "pets.json", CONTENT / "creatures.json"
    elif args.assets:
        CONTENT_WRITABLE = False  # a trial on copied art must not write the checkout's content/
    game_preview.configure(REPO, SOURCE, CONTENT)
    prepare()
    if args.git_branch:
        global GIT
        GIT = GitSync(REPO, args.git_branch, push=args.git_push, base=args.git_base, write_lock=LOCK)
        recovered = GIT.recover()
        if recovered:
            print(f"Committed studio edits a previous run left uncommitted ({recovered})", flush=True)
        if args.git_push:
            GIT.request_push()  # unpushed commits from a previous run go out now, not on the next save
    server = StudioServer((args.host, args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"Jelli Art {STUDIO_VERSION}: {url}  (Ctrl+C to stop)", flush=True)
    print(f"Serving {SOURCE}" + (f"; committing saves to {args.git_branch}" if GIT else ""), flush=True)
    if not args.no_open:
        threading.Timer(0.4, webbrowser.open, (url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
