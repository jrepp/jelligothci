"""Fixtures for the Jelli Art UI tests.

The studio serves art and content extracted once from the pinned revision in fixture.json into a
fixed directory (labels in the page show its path), and a copy of the same art is the before image,
so art changes never move a baseline. The server gets an empty PATH, so capability-dependent views
(Behaviour editing, Test in game) look the same on every machine. Screenshots are always saved to build/ui-tests/screenshots, but compared with
(or, with JELLI_UI_UPDATE=1, written to) the baselines only when JELLI_UI_SCREENSHOTS=1, which CI
sets: the linux/amd64 Playwright image is the reference renderer. JELLI_UI_UPDATE=1 rewrites the
aria and axe baselines instead of comparing.
"""
import fcntl
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import tarfile
import time
import urllib.request
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
JELLI_ART = HERE.parent
REPO = JELLI_ART.parents[1]
FIXTURE = json.loads((HERE / "fixture.json").read_text())
BASELINES = HERE / "baselines"
OUT = Path(os.environ.get("JELLI_UI_OUT") or REPO / "build/ui-tests")
DATA = Path("/tmp/jelli-ui-tests")  # fixed, because the page shows the before path in its labels
UPDATE = os.environ.get("JELLI_UI_UPDATE") == "1"
SCREENSHOTS = os.environ.get("JELLI_UI_SCREENSHOTS") == "1"
VIEWS = FIXTURE["views"]
VIEWPORTS = FIXTURE["viewports"]
CASES = [(view, viewport) for view in VIEWS for viewport in VIEWPORTS]
BUDGET = {}  # "view@viewport" -> share of the viewport given to the view's primary surface
NOTES = []  # printed after the run, such as baselines that can be lowered

# Header items that change with the checkout (commit list, version, git state) stay out of baselines.
VOLATILE = ["#before-ref", "#live-badge", "#git-status", "#studio-status", "#shell-toasts"]


def case_id(view, viewport):
    return f"{view}@{viewport}"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


# Containers run as a different user than the checkout's owner; let git read it anyway.
GIT_SAFE = {"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "safe.directory", "GIT_CONFIG_VALUE_0": "*"}


def extract_data(root):
    """Art and content at the pinned revision, from git, into root."""
    ref = FIXTURE["data_ref"]
    run = subprocess.run(["git", "-C", str(REPO), "archive", ref, "assets/slice", "content"],
                         capture_output=True, env={**os.environ, **GIT_SAFE})
    if run.returncode:
        raise RuntimeError(f"git archive {ref} failed (fetch full history?): {run.stderr.decode().strip()}")
    archive = run.stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(root, filter="data")


@pytest.fixture(scope="session")
def browser_type_launch_args(browser_type_launch_args):
    """Software rendering only, so screenshots do not depend on a GPU (or on its absence under emulation)."""
    return {**browser_type_launch_args, "args": ["--disable-gpu", "--force-color-profile=srgb", "--font-render-hinting=none"]}


@pytest.fixture(scope="session")
def studio():
    """URL of a jelli_art.py process serving the pinned data, with a copy of it as the before image."""
    # DATA is a fixed path, so concurrent runs on one machine take turns.
    lock = open(f"{DATA}.lock", "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print(f"Waiting for another UI test run to release {DATA}.lock", file=sys.stderr)
        fcntl.flock(lock, fcntl.LOCK_EX)
    shutil.rmtree(DATA, ignore_errors=True)
    extract_data(DATA)
    shutil.copytree(DATA / "assets/slice", DATA / "before")
    bindir = DATA / "bin"  # empty: no git, cmake or compiler, the same on every machine
    bindir.mkdir()
    port = free_port()
    env = {**os.environ, "PATH": str(bindir)}
    OUT.mkdir(parents=True, exist_ok=True)
    log = open(OUT / "studio.log", "wb")
    proc = subprocess.Popen([sys.executable, str(JELLI_ART / "jelli_art.py"), "--no-open", "--port", str(port),
                             "--assets", str(DATA / "assets/slice"), "--content", str(DATA / "content")],
                            stdout=log, stderr=subprocess.STDOUT, env=env)
    url = f"http://127.0.0.1:{port}"
    deadline = time.monotonic() + 60
    while True:
        if proc.poll() is not None:
            raise RuntimeError(f"studio exited; see {OUT / 'studio.log'}")
        try:
            urllib.request.urlopen(url + "/healthz", timeout=2).read()
            break
        except OSError:
            if time.monotonic() > deadline:
                proc.kill()
                raise RuntimeError(f"studio did not answer /healthz; see {OUT / 'studio.log'}")
            time.sleep(0.2)
    yield url
    proc.terminate()
    proc.wait(timeout=10)
    log.close()
    lock.close()


@pytest.fixture
def open_view(studio, browser):
    """open_view(view, viewport, scheme='dark') -> a page showing that view, settled for snapshots."""
    contexts = []

    def opener(view, viewport, scheme="dark"):
        width, height = VIEWPORTS[viewport]
        context = browser.new_context(viewport={"width": width, "height": height}, device_scale_factor=1,
                                      color_scheme=scheme, reduced_motion="reduce", locale="en-US",
                                      timezone_id="UTC")
        contexts.append(context)
        ref = json.dumps(str(DATA / "before"))
        context.add_init_script(f"try {{ localStorage.setItem('jelli-shell:guide', 'false');"
                                f" localStorage.setItem('jelli-review:before', {json.dumps(ref)}); }} catch {{}}")
        page = context.new_page()
        events = []
        page.on("console", lambda m: events.append(f"console.{m.type}: {m.text}"))
        page.on("pageerror", lambda e: events.append(f"pageerror: {e}"))
        page.on("requestfailed", lambda r: events.append(f"requestfailed: {r.url} {r.failure}"))
        page.goto(f"{studio}/#{VIEWS[view]['hash']}")
        # D is the page's top-level data binding (a script-scope let, not a window property).
        try:
            page.wait_for_function("() => typeof D !== 'undefined' && D && D.assets && D.assets.length > 0")
        except Exception as error:
            state = page.evaluate("() => ({assets: typeof D === 'undefined' ? null : (D.assets || []).length,"
                                  " title: document.title, main: document.querySelector('main')?.innerText.slice(0, 300)})")
            raise AssertionError(f"{view}@{viewport}: page data never loaded: {state}; events: {events[-20:]}") from error
        page.wait_for_load_state("networkidle")
        page.locator(VIEWS[view]["primary"]).first.wait_for(state="attached")
        page.evaluate("() => document.fonts.ready.then(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))))")
        return page

    yield opener
    for context in contexts:
        context.close()


def pytest_terminal_summary(terminalreporter):
    if NOTES:
        terminalreporter.section("Notes")
        for note in NOTES:
            terminalreporter.write_line(note)
    if not BUDGET:
        return
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "chrome-budget.json").write_text(json.dumps(BUDGET, indent=2, sort_keys=True) + "\n")
    terminalreporter.section("Chrome budget (primary surface share of the viewport, report only)")
    for key in sorted(BUDGET):
        terminalreporter.write_line(f"{key:32} {BUDGET[key] * 100:5.1f}%")
