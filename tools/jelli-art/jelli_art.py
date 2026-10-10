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
import shutil
import subprocess
import sys
import threading
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
import game_preview  # noqa: E402
import profiles  # noqa: E402
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
CONTENT = REPO / "content"
PETS, CREATURE_DATA = CONTENT / "pets.json", CONTENT / "creatures.json"
CONTENT_WRITABLE = True  # False for --assets trials without --content, so a trial never edits the checkout
TEMPLATE = HERE.parent / "assets/compare.html"
STUDIO_JS = HERE / "studio.js"
CREATURE_JS = HERE / "creature.js"
BEHAVIOUR_JS = HERE / "behaviour.js"
SHELL_JS = HERE / "shell.js"  # first: the page frame and window.JelliShell, which later scripts use
PAGE_SCRIPTS = (SHELL_JS, STUDIO_JS, CREATURE_JS, HERE / "animation.js", BEHAVIOUR_JS, HERE / "simulator.js",
                HERE / "reactions.js", HERE / "game_preview.js")
EDITABLE_CONTENT = ("behaviors", "creatures")
STUDIO_VERSION = (HERE / "VERSION").read_text().strip()
GIT = None  # GitSync when committing saves
MAX_BODY = 1 << 20
LOCK = threading.Lock()


class StudioError(ValueError):
    """A request the studio refuses, reported to the page as a 400."""


def read_manifest():
    return json.loads(MANIFEST.read_text())


def write_json(path, value):
    """Atomic replace so the live game watcher never sees a half-written file."""
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, indent=2) + "\n")
    temp.replace(path)


def version():
    """Changes whenever the manifest, creature content or any PNG changes on disk."""
    digest = hashlib.sha1()
    content = [PETS, CREATURE_DATA, *(CONTENT / f"{n}.json" for n in ("behaviors", "activities", "potty"))]
    for path in [MANIFEST, *[p for p in content if p.exists()], *sorted(SOURCE.glob("*/*.png"))]:
        stat = path.stat()
        digest.update(f"{path.name}{stat.st_mtime_ns}{stat.st_size}".encode())
    return digest.hexdigest()[:16]


def hand_painted():
    return json.loads(HAND_PAINTED.read_text()) if HAND_PAINTED.exists() else []


def payload(before):
    manifest, records = collect(before, SOURCE)
    painted = set(hand_painted())
    details = creatures.asset_details(manifest)
    for record in records:
        record["hand_painted"] = record["key"] in painted
        record.update(details[record["key"]])
    return {"live": True, "before_label": before, "after_label": "working tree", "version": version(),
            "studio_version": STUDIO_VERSION, "palette": manifest["palette"], "assets": records,
            **creatures.creature_data(manifest, PETS), **creature_profiles(), **behaviour_data(),
            "git": GIT.status() if GIT else {"enabled": False}}


def content_paths():
    """The content files the studio may write, by name."""
    return {"behaviors": CONTENT / "behaviors.json", "creatures": CREATURE_DATA}


def behaviour_data():
    path = content_paths()["behaviors"]
    vocab = behaviors.vocabulary(REPO, CONTENT)
    editable = CONTENT_WRITABLE and vocab is not None and path.exists() and shutil.which("cmake") is not None
    return {"behavior_data": behaviors.read_json(path), "behavior_data_sha": profiles.digest(path),
            "behavior_vocab": vocab, "behavior_editable": editable,
            "potty": behaviors.read_json(CONTENT / "potty.json")}


def creature_profiles():
    data = profiles.profile_data(CREATURE_DATA, creatures.load_checkout_module(REPO, "creature_data"))
    data["creature_data_editable"] = data["creature_data_editable"] and CONTENT_WRITABLE
    return data


def refs():
    def git(*args):
        result = subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True, check=False)
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
    if len(pixels) != w * h:
        raise StudioError(f"Expected {w * h} pixels for {asset['key']}")
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for i, color in enumerate(pixels):
        if color is None:
            continue
        if not (isinstance(color, str) and len(color) == 7 and color[0] == "#"):
            raise StudioError("Pixels must be '#rrggbb' or null")
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
        return {"git_error": str(error)}


def save_asset(key, pixels, artist=""):
    with LOCK:
        manifest = read_manifest()
        asset = find_asset(manifest, key)
        if asset["kind"] in ("font", "backgrounds"):
            raise StudioError("Font and backgrounds are not editable in the studio yet")
        image = image_from_pixels(asset, pixels)
        image.save(SOURCE / asset["path"])
        asset["bounds"] = list(image.getchannel("A").getbbox())
        write_json(MANIFEST, manifest)
        painted = hand_painted()
        if key not in painted:
            write_json(HAND_PAINTED, sorted([*painted, key]))
        git = record([SOURCE / asset["path"], MANIFEST, HAND_PAINTED], f"chore(art): repaint {key} in Jelli Art", artist)
    return {"ok": True, "version": version(), **git}


def tidy(key, pixels):
    """The polish re-ink pass: closed 1px outline, no specks, bottom shadow on 32px art."""
    if reink is None:
        raise StudioError("This checkout has no polish recipe, so Tidy is unavailable")
    asset = find_asset(read_manifest(), key)
    if "palette" in asset:
        raise StudioError("Tidy inks with the shared palette; this sprite has its own palette")
    sprite = reink(image_from_pixels(asset, pixels))
    return {"pixels": pixels_from_image(to_image(sprite, (asset["width"], asset["height"])))}


def save_clips(edits, artist=""):
    """Change frames, durations and loop of existing creature clips; nothing else in the manifest moves."""
    with LOCK:
        original = read_manifest()
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
    return {"ok": True, "changed": changed, "version": version(), **({"warning": warning} if warning else {}), **git}


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
                raise StudioError(f"content/{name}.json changed since you loaded it; revert to load the new version")
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
            for name, doc in changed.items():
                write_json(paths[name], doc)
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


def set_palette_slot(index, color, artist=""):
    """Recolour one shared slot everywhere it is used, then update the manifest."""
    with LOCK:
        manifest = read_manifest()
        palette = manifest["palette"]
        color = color.lower()
        if not (0 <= index < len(palette)):
            raise StudioError("Unknown palette slot")
        if len(color) != 7 or color[0] != "#" or color in palette:
            raise StudioError("Choose a colour that is not already in the palette")
        old = tuple(bytes.fromhex(palette[index][1:]))
        new = tuple(bytes.fromhex(color[1:]))
        changed = []
        for asset in manifest["assets"]:
            if "palette" in asset:  # backgrounds keep their own grey palette
                continue
            path = SOURCE / asset["path"]
            image = Image.open(path).convert("RGBA")
            data = [(new + (255,)) if p[3] and p[:3] == old else p for p in image.getdata()]
            if data != list(image.getdata()):
                image.putdata(data)
                image.save(path)
                changed.append(asset["key"])
        palette[index] = color
        write_json(MANIFEST, manifest)
        paths = [SOURCE / a["path"] for a in manifest["assets"] if a["key"] in changed] + [MANIFEST]
        git = record(paths, f"chore(art): change palette slot {index} to {color} in Jelli Art", artist)
    return {"ok": True, "changed": changed, "version": version(), **git}


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
        length = int(self.headers.get("Content-Length", 0))
        if length > MAX_BODY:
            raise StudioError("Request too large")
        return json.loads(self.rfile.read(length) or b"{}")

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
            return self.send(HTTPStatus.OK, {"ok": True, "studio_version": STUDIO_VERSION})
        if method == "GET" and url.path == "/api/git":
            return self.send(HTTPStatus.OK, GIT.status() if GIT else {"enabled": False})
        if method == "GET" and url.path == "/api/version":
            return self.send(HTTPStatus.OK, {"version": version()})
        if method == "GET" and url.path == "/api/refs":
            return self.send(HTTPStatus.OK, refs())
        if method == "POST" and url.path == "/api/save":
            body = self.body()
            return self.send(HTTPStatus.OK, save_asset(body["key"], body["pixels"], str(body.get("artist", ""))))
        if method == "POST" and url.path == "/api/tidy":
            body = self.body()
            return self.send(HTTPStatus.OK, tidy(body["key"], body["pixels"]))
        if method == "GET" and url.path == "/api/clips":
            return self.send(HTTPStatus.OK, creatures.creature_data(read_manifest(), PETS))
        if method == "POST" and url.path == "/api/clips":
            body = self.body()
            return self.send(HTTPStatus.OK, save_clips(body["clips"], str(body.get("artist", ""))))
        if method == "GET" and url.path == "/api/frames":
            return self.send(HTTPStatus.OK, frames_overview())
        if method == "POST" and url.path == "/api/frames":
            body = self.body()
            return self.send(HTTPStatus.OK, edit_frame(body, str(body.get("artist", ""))))
        if method == "GET" and url.path == "/api/creatures":
            return self.send(HTTPStatus.OK, creature_profiles())
        if method == "POST" and url.path == "/api/creatures":
            body = self.body()
            return self.send(HTTPStatus.OK, save_creature_data(body["data"], body.get("base"), str(body.get("artist", ""))))
        if method == "GET" and url.path == "/api/behaviour":
            return self.send(HTTPStatus.OK, {**behaviour_data(), **creature_profiles()})
        if method == "POST" and url.path == "/api/content":
            body = self.body()
            return self.send(HTTPStatus.OK, save_content(body["docs"], body.get("bases"), str(body.get("artist", ""))))
        if method == "POST" and url.path == "/api/palette":
            body = self.body()
            return self.send(HTTPStatus.OK, set_palette_slot(int(body["index"]), str(body["color"]), str(body.get("artist", ""))))
        return self.send(HTTPStatus.NOT_FOUND, {"error": "Not found"})

    def handle_method(self, method):
        try:
            self.route(method)
        except (StudioError, KeyError, ValueError, TypeError) as error:
            self.send(HTTPStatus.BAD_REQUEST, {"error": str(error)})

    def do_GET(self):  # noqa: N802 (http.server naming)
        self.handle_method("GET")

    def do_POST(self):  # noqa: N802
        self.handle_method("POST")

    def log_message(self, fmt, *args):
        if "/api/version" not in self.path:
            sys.stderr.write(f"  {self.command} {self.path.split('?')[0]} {args[1] if len(args) > 1 else ''}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="bind address (the container uses 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true", help="do not open a browser tab")
    parser.add_argument("--assets", type=Path, help="serve a copy laid out like assets/slice (for trials)")
    parser.add_argument("--content", type=Path, help="read and write a copy laid out like content/ (for trials)")
    parser.add_argument("--git-branch", help="commit every save to this branch of the checkout")
    parser.add_argument("--git-push", action="store_true", help="push --git-branch to origin after commits")
    args = parser.parse_args()
    if args.assets:
        if args.git_branch:
            parser.error("--assets trials cannot be combined with --git-branch")
        global SOURCE, MANIFEST, HAND_PAINTED
        SOURCE = args.assets.resolve()
        MANIFEST, HAND_PAINTED = SOURCE / "assets.json", SOURCE / "source/hand-painted.json"
    global CONTENT, PETS, CREATURE_DATA, CONTENT_WRITABLE
    if args.content:
        if args.git_branch:
            parser.error("--content trials cannot be combined with --git-branch")
        CONTENT = args.content.resolve()
        PETS, CREATURE_DATA = CONTENT / "pets.json", CONTENT / "creatures.json"
    elif args.assets:
        CONTENT_WRITABLE = False  # a trial on copied art must not write the checkout's content/
    game_preview.configure(REPO, SOURCE, CONTENT)
    if args.git_branch:
        global GIT
        GIT = GitSync(REPO, args.git_branch, push=args.git_push)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
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
