"""UX M2-A: task-first shell chrome (RFC-006 goals 1-3, layout spec M2). One header row in every mode and a second only in
Review; everything that changes while you work (the Unsaved pill, tab dots, git status, Paint's title row) has a reserved slot,
so the canvas never moves under the pointer; the Getting started card is capped and keeps its Hide button in view; "Your name"
lives in a Settings popover and is asked for once before the first save when it is empty.

Sizes are the spec's: 1440x900 and 1280x800 with a mouse, the iPad pair with touch.

Run: tools/jelli-art/ui_tests/run.sh -k test_layout
"""
import json

import pytest

from conftest import DATA, NOTES, VIEWS, settle
from touch import pen_stroke

PAINT = "paint-creature"
SIZES = {"1440x900": (1440, 900, False), "1280x800": (1280, 800, False), "ipad-landscape": (1180, 820, True),
         "ipad-portrait": (820, 1180, True)}
MODES = ["review", "paint", "creature", "behaviour", "game", "activities"]


def open_at(browser, studio, size, view=PAINT):
    """(context, page) for a view at one of SIZES; the caller closes the context."""
    width, height, touch = SIZES[size]
    context = browser.new_context(viewport={"width": width, "height": height}, device_scale_factor=2 if touch else 1, has_touch=touch,
                                  color_scheme="dark", reduced_motion="reduce", locale="en-US", timezone_id="UTC")
    try:
        return context, settle(context, studio, view, size)
    except BaseException:
        context.close()
        raise


@pytest.fixture
def page_at(browser, studio):
    contexts = []

    def opener(size, view=PAINT):
        context, page = open_at(browser, studio, size, view)
        contexts.append(context)
        return page

    yield opener
    for context in contexts:
        context.close()


def header(page):
    return page.evaluate("() => document.querySelector('header').getBoundingClientRect().height")


def dirty(page, mode="paint", n=3):
    page.evaluate("""([mode, n]) => { window.__dirty = n;
        JelliShell.registerDirty('layout-dirty', {label: 'Paint', mode, check: () => window.__dirty}); }""", [mode, n])


# ---------- header: one row, a second only in Review ----------

@pytest.mark.parametrize("size, row, review", [("1440x900", 56, 96), ("1280x800", 56, 96), ("ipad-landscape", 64, 116)])
def test_header_height_by_mode_and_dirty_state(page_at, size, row, review):
    page = page_at(size)
    heights = {}
    for state in ("clean", "dirty"):
        for mode in MODES:
            page.evaluate("id => JelliShell.activateMode(id)", mode)
            page.wait_for_timeout(60)
            heights[state, mode] = header(page)
        if state == "clean":
            dirty(page)
            assert page.locator("#shell-unsaved").is_visible()
    NOTES.append(f"header height {size}: " + ", ".join(f"{mode} {heights['clean', mode]:.0f}/{heights['dirty', mode]:.0f}" for mode in MODES) + " (clean/dirty px)")
    for mode in MODES:
        budget = review if mode == "review" else row
        assert heights["clean", mode] <= budget, f"{size} {mode}: header is {heights['clean', mode]:.0f}px, budget {budget}"
        assert abs(heights["clean", mode] - heights["dirty", mode]) <= 1, f"{size} {mode}: {heights['clean', mode]} -> {heights['dirty', mode]}"


def test_row_two_is_review_only_and_lint_totals_leave_paint(page_at):
    page = page_at("1440x900", "review-detail")
    visible = "sel => !!document.querySelector(sel)?.checkVisibility()"
    for sel in (".refs", "#totals", "#before-ref"):
        assert page.evaluate(visible, f"header {sel}"), f"{sel} should show in Review"
    page.evaluate("() => JelliShell.activateMode('paint')")
    page.wait_for_timeout(100)
    assert page.evaluate("() => document.documentElement.dataset.mode") == "paint"
    for sel in (".refs", "#totals"):
        assert not page.evaluate(visible, f"header {sel}"), f"{sel} should be Review-only"
    # The one compare-with control moves into Paint's before pane (bound to the same select), so nothing is lost.
    assert page.evaluate(visible, "#stage #before-ref")
    assert page.locator("header #before-ref").count() == 0
    page.evaluate("() => JelliShell.activateMode('review')")
    page.wait_for_timeout(100)
    assert page.evaluate(visible, "header #before-ref") and page.locator("#stage #before-ref").count() == 0


def test_data_mode_follows_the_active_tab(page_at):
    page = page_at("1440x900", "review-detail")
    for mode in MODES:
        page.evaluate("id => JelliShell.activateMode(id)", mode)
        page.wait_for_timeout(40)
        assert page.evaluate("() => document.documentElement.dataset.mode") == mode


def test_save_state_cluster_sits_before_the_tools_on_one_row(page_at):
    page = page_at("1440x900")
    boxes = page.evaluate("""() => Object.fromEntries(['#shell-modes', '#git-status', '#shell-unsaved', '#shell-settings-button',
        '#shell-help-button', '#shell-guide-button', '#shell-theme'].map(s => { const r = document.querySelector(s).getBoundingClientRect();
        return [s, [Math.round(r.left), Math.round(r.top), Math.round(r.right), Math.round(r.bottom)]]; }))""")
    names = ["#shell-modes", "#shell-unsaved", "#shell-settings-button", "#shell-help-button", "#shell-guide-button", "#shell-theme"]
    for a, b in zip(names, names[1:]):
        assert boxes[a][2] <= boxes[b][0] + 1, f"{a} should end before {b} starts: {boxes}"
    assert max(boxes[n][1] for n in names) - min(boxes[n][1] for n in names) < 30, boxes  # one row


# ---------- no layout shift ----------

def canvas(page):
    return page.evaluate("""() => { const c = document.querySelector('#stage .paint-canvas canvas'), r = c.getBoundingClientRect();
        return {x: r.left, y: r.top, z: Math.round(r.width / byKey[state.key].width * 100) / 100, stage: document.getElementById('stage').getBoundingClientRect().top, scroll: document.getElementById('main').scrollTop, header: document.querySelector('header').getBoundingClientRect().height}; }""")


def paint_something(page, coarse, row=0.3):
    c = page.evaluate("""() => { const r = document.querySelector('#stage .paint-canvas canvas').getBoundingClientRect();
        return {x: r.left, y: r.top, w: r.width, h: r.height}; }""")
    pts = [(c["x"] + c["w"] * 0.3, c["y"] + c["h"] * row), (c["x"] + c["w"] * 0.6, c["y"] + c["h"] * row)]
    if coarse:
        pen_stroke(page, pts)
    else:
        page.mouse.move(*pts[0])
        page.mouse.down()
        page.mouse.move(*pts[1], steps=6)
        page.mouse.up()
    page.wait_for_timeout(120)


def same_size_neighbour(page):
    return page.evaluate("""() => { const a = byKey[state.key]; return D.assets.find(b => b.key !== a.key && b.kind === a.kind && b.width === a.width && b.height === a.height)?.key; }""")


@pytest.mark.parametrize("size", ["1440x900", "1280x800", "ipad-landscape"])
def test_the_canvas_never_moves_while_you_work(page_at, size):
    page = page_at(size)
    coarse = SIZES[size][2]
    other = same_size_neighbour(page)
    assert other, "the fixture needs a second asset of the same size"
    version = page.evaluate("() => D.version")
    page.route("**/api/save", lambda route: route.fulfill(status=200, content_type="application/json",
                                                           body=json.dumps({"sha": "mock-sha", "version": version, "commit": None})))
    page.evaluate("() => Studio.renderGit({enabled: true, branch: 'main', push: true, unpushed: 0, last_push: {ok: true}})")
    page.wait_for_timeout(100)
    # The first fit is taken from the stage's top while the page is still settling (touch chrome joins the header after it), so it can be a
    # step off the settled one on a touch screen. Measuring fit against a CSS box after layout is the Paint workspace chunk (M2-B).
    page.evaluate("() => renderStage()")
    seen = [("start", canvas(page))]

    def step(name, act):
        act()
        page.wait_for_timeout(250)
        seen.append((name, canvas(page)))

    step("first stroke", lambda: paint_something(page, coarse))
    assert page.locator("#asset-stats .stat", has_text="unsaved edits").count() == 1
    step("save", lambda: page.locator("#save").click())
    assert page.locator("#save").inner_text() == "Saved"
    step("dirty again", lambda: paint_something(page, coarse, 0.6))
    key = page.evaluate("() => state.key")
    step("to a clean asset", lambda: page.evaluate("k => select(k)", other))
    step("and back to the dirty one", lambda: page.evaluate("k => select(k)", key))
    step("git status goes bad", lambda: page.evaluate(
        "() => Studio.renderGit({enabled: true, branch: 'main', push: true, unpushed: 2, last_push: {ok: false, error: 'permission denied (publickey)'}})"))
    step("a toast", lambda: page.evaluate("() => JelliShell.notify('Saved the thing, but the commit failed: nothing to commit', {tone: 'bad'})"))
    assert page.locator(".shell-toast").count() >= 1
    moves = []
    for (_, before), (name, after) in zip(seen, seen[1:]):
        moved = (abs(after["x"] - before["x"]), abs(after["y"] - before["y"]), abs(after["header"] - before["header"]))
        if max(moved) > 2:
            moves.append(f"{name}: canvas moved {moved[0]:.0f},{moved[1]:.0f} px and the header {moved[2]:.0f} px")
        if after["z"] != before["z"]:
            moves.append(f"{name}: zoom {before['z']} -> {after['z']}")
    assert not moves, "\n".join(moves) + f"\n{seen}"


# ---------- Getting started ----------

def first_visit(browser, studio, size):
    """A page whose guide has never been hidden (settle() hides it)."""
    width, height, touch = SIZES[size]
    context = browser.new_context(viewport={"width": width, "height": height}, device_scale_factor=2 if touch else 1, has_touch=touch,
                                  reduced_motion="reduce", locale="en-US", timezone_id="UTC")
    context.add_init_script(f"try {{ localStorage.setItem('jelli-review:before', {json.dumps(json.dumps(str(DATA / 'before')))});"
                            " if (localStorage.getItem('jelli-review:artist') === null) localStorage.setItem('jelli-review:artist', '\"Test Artist\"'); } catch {}")
    page = context.new_page()
    page.goto(f"{studio}/#{VIEWS[PAINT]['hash']}")
    page.wait_for_function("() => typeof D !== 'undefined' && D && D.assets && D.assets.length > 0 && document.body.dataset.ready")
    page.wait_for_load_state("networkidle")
    return context, page


@pytest.mark.parametrize("size", ["1280x800", "ipad-landscape"])
def test_the_guide_is_capped_keeps_hide_in_view_and_stays_hidden(browser, studio, size):
    context, page = first_visit(browser, studio, size)
    try:
        guide = page.locator("#shell-guide")
        assert guide.is_visible()
        box = guide.bounding_box()
        vh = SIZES[size][1]
        assert box["height"] <= min(0.35 * vh, 320) + 1, box
        hide = page.get_by_role("button", name="Hide this guide")
        hb = hide.bounding_box()
        assert 0 <= hb["y"] and hb["y"] + hb["height"] <= vh, hb
        assert hb["y"] + hb["height"] <= box["y"] + box["height"], "Hide is part of the heading row, not scrolled away inside the card"
        hide.click()
        assert not guide.is_visible()
        page.reload()
        page.wait_for_function("() => document.body.dataset.ready")
        assert not page.locator("#shell-guide").is_visible()
        page.locator("#shell-guide-button").click()
        assert page.locator("#shell-guide").is_visible()
    finally:
        context.close()


# ---------- Your name: a Settings popover, asked for before the first save ----------

def test_your_name_lives_in_settings_not_the_header(page_at):
    page = page_at("1440x900")
    assert page.locator("header #artist").count() == 0 or not page.locator("header #artist").is_visible()
    assert not page.locator("#artist").is_visible()
    button = page.get_by_role("button", name="Settings")
    button.click()
    field = page.get_by_label("Your name")
    assert field.is_visible()
    assert field.input_value() == "Test Artist"
    field.fill("Ada")
    page.keyboard.press("Escape")
    assert not field.is_visible()
    assert page.evaluate("() => state.artist") == "Ada"
    page.reload()
    page.wait_for_function("() => document.body.dataset.ready")
    page.get_by_role("button", name="Settings").click()
    assert page.get_by_label("Your name").input_value() == "Ada"


def save_with_empty_name(page, answer):
    sent = []
    version = page.evaluate("() => D.version")

    def fulfil(route):
        sent.append(json.loads(route.request.post_data or "{}"))
        route.fulfill(status=200, content_type="application/json", body=json.dumps({"sha": f"mock-{len(sent)}", "version": version, "commit": None}))

    page.route("**/api/save", fulfil)
    page.evaluate("() => { state.artist = ''; localStorage.removeItem('jelli-review:artist'); }")
    paint_something(page, False)
    page.locator("#save").click()
    dialog = page.locator("dialog.shell-dialog[open]")
    dialog.wait_for()
    assert dialog.get_by_role("heading", name="Who is saving?").is_visible()
    assert not sent, "the save waits for the answer"
    answer(dialog)
    page.wait_for_function("() => document.getElementById('save').textContent === 'Saved'")
    return sent


def test_an_empty_name_is_asked_for_once_before_the_first_save(page_at):
    page = page_at("1440x900")
    def type_name(dialog):
        dialog.get_by_label("Your name").fill("Grace")
        page.keyboard.press("Enter")
    sent = save_with_empty_name(page, type_name)
    assert [s["artist"] for s in sent] == ["Grace"]
    assert page.evaluate("() => localStorage.getItem('jelli-review:artist')") == '"Grace"'
    paint_something(page, False, 0.6)
    page.locator("#save").click()
    page.wait_for_timeout(300)
    assert page.locator("dialog.shell-dialog[open]").count() == 0, "asked only once"
    assert [s["artist"] for s in sent] == ["Grace", "Grace"]


def test_a_scrolling_stats_row_is_keyboard_reachable(page_at):
    page = page_at("1440x900")
    page.set_viewport_size({"width": 700, "height": 900})
    page.wait_for_timeout(150)
    stats = page.evaluate("() => { const e = document.getElementById('asset-stats'); return {over: e.scrollWidth > e.clientWidth + 1, tabindex: e.getAttribute('tabindex'), role: e.getAttribute('role')}; }")
    assert stats["over"], "the Paint stat pills should overflow their row at 700 px"
    assert stats["tabindex"] == "0" and stats["role"] == "group", stats
    page.set_viewport_size({"width": 1920, "height": 1080})
    page.wait_for_timeout(150)
    assert page.evaluate("() => document.getElementById('asset-stats').getAttribute('tabindex')") is None


def test_skipping_the_name_still_saves_and_does_not_nag(page_at):
    page = page_at("1440x900")
    sent = save_with_empty_name(page, lambda dialog: dialog.get_by_role("button", name="Skip").click())
    assert [s["artist"] for s in sent] == [""]
    paint_something(page, False, 0.6)
    page.locator("#save").click()
    page.wait_for_timeout(300)
    assert page.locator("dialog.shell-dialog[open]").count() == 0
    assert len(sent) == 2


def test_a_git_problem_leads_the_status_so_the_ellipsis_cannot_hide_it(page_at):
    """The git status slot has a fixed width and an ellipsis (no layout shift); the failure must be the part that stays visible."""
    page = page_at("1280x800")
    page.evaluate("() => Studio.renderGit({enabled: true, branch: 'fix/a-very-long-branch-name-for-a-feature', push: true, unpushed: 2, last_push: {ok: false, error: 'denied'}})")
    text = page.locator("#git-status").inner_text()
    assert text.startswith("⚠ push failing"), text
    page.evaluate("() => Studio.renderGit({enabled: true, branch: 'fix/a-very-long-branch-name-for-a-feature', push: true, unpushed: 0, last_push: {ok: true}, pending_commits: 2, last_commit_error: 'locked'})")
    assert page.locator("#git-status").inner_text().startswith("⚠ 2 commit(s) queued")


def test_settings_and_the_name_prompt_work_from_the_keyboard(page_at):
    page = page_at("1440x900")
    button = page.get_by_role("button", name="Settings")
    button.focus()
    page.keyboard.press("Enter")
    page.wait_for_function("() => document.activeElement?.id === 'artist'")
    assert page.get_by_label("Your name").evaluate("e => e === document.activeElement"), "opening Settings moves focus into it"
    page.keyboard.press("Escape")
    assert not page.get_by_label("Your name").is_visible()
    assert button.evaluate("e => e === document.activeElement"), "Escape returns focus to the Settings button"
    # Mod+S with no name asks once; Escape skips and the save still happens (no keyboard trap, no loop).
    version = page.evaluate("() => D.version")
    sent = []
    page.route("**/api/save", lambda route: (sent.append(1), route.fulfill(status=200, content_type="application/json",
                                                                          body=json.dumps({"sha": "m", "version": version, "commit": None})))[1])
    page.evaluate("() => { state.artist = ''; localStorage.removeItem('jelli-review:artist'); }")
    paint_something(page, False)
    page.locator("#stage .paint-canvas canvas").focus()
    page.keyboard.press("ControlOrMeta+s")
    page.locator("dialog.shell-dialog[open]").wait_for()
    page.keyboard.press("Escape")
    page.wait_for_function("() => document.getElementById('save').textContent === 'Saved'")
    assert len(sent) == 1
