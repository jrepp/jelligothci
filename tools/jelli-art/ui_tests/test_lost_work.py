"""UX M1: no lost work and no wrong view. Every ordered pair of views shows exactly one view root; Activities joins the
shell's unsaved tracking; Creature, Behaviour and Activities edits survive a reload as drafts; Revert and Reload saved
ask first and only discard their own editor's edits; Test in game says it renders the saved files.

Run: tools/jelli-art/ui_tests/run.sh -k lost_work
"""
import json
from itertools import permutations

import pytest
from playwright.sync_api import expect

from conftest import new_view, settle

MODES = ["review", "paint", "creature", "behaviour", "game", "activities"]
ROOT_OF = {"review": "detail", "paint": "detail", "creature": "creature", "behaviour": "behaviour", "game": "game",
           "activities": "activities"}
ROOTS = ["detail", "sheet", "creature", "behaviour", "game", "activities"]
FOCUSABLE = "a[href], button, input, select, textarea, summary, [tabindex]:not([tabindex='-1'])"
PIXEL = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="


@pytest.fixture
def studio_page(browser, studio):
    """A desktop page on the Review view and its context; a test may reopen the page to simulate a reload."""
    context, page = new_view(browser, studio, "review-detail", "desktop")
    box = {"page": page}

    def reopen():
        box["page"].close()  # closing runs no beforeunload prompt: this is what a Safari tab eviction looks like
        box["page"] = settle(context, studio, "review-detail", "desktop")
        return box["page"]

    box["reopen"] = reopen
    yield box
    context.close()


def go(page, mode):
    page.evaluate("id => JelliShell.activateMode(id)", mode)
    page.wait_for_timeout(60)


def wait_draft(page, kind):
    page.wait_for_function("k => Object.keys(localStorage).some(n => n.startsWith('jelli-art:draft:' + k + ':'))", arg=kind)


def toast(page, text):
    return page.locator("#shell-toasts").get_by_text(text)


def dialog(page):
    return page.locator("dialog.shell-dialog[open]")


def unsaved_button(page):
    return page.locator("#shell-unsaved")


# ---------- C1: one view at a time ----------

def test_every_ordered_pair_of_views_shows_exactly_one_root(studio_page):
    page = studio_page["page"]
    problems = []
    for a, b in permutations(MODES, 2):
        go(page, a)
        go(page, b)
        seen = page.evaluate("""([roots, focusable]) => {
            const shown = roots.filter(id => getComputedStyle(document.getElementById(id)).display !== 'none');
            const leaks = roots.filter(id => !shown.includes(id)).flatMap(id =>
              [...document.getElementById(id).querySelectorAll(focusable)].filter(el => el.checkVisibility()).map(el => id + ' ' + el.tagName));
            return {shown, leaks}; }""", [ROOTS, FOCUSABLE])
        if seen["shown"] != [ROOT_OF[b]] or seen["leaks"]:
            problems.append(f"{a} -> {b}: shown {seen['shown']}, focusable in hidden views {seen['leaks']}")
    assert not problems, "\n".join(problems)


@pytest.mark.parametrize("hash_, root", [("view=game", "game"), ("view=activities", "activities"), ("view=creature", "creature"),
                                         ("view=behaviour", "behaviour"), ("view=sheet", "sheet"), ("paint", "detail"), ("review", "detail")])
def test_a_deep_link_lands_on_exactly_one_view(studio_page, studio, hash_, root):
    page = studio_page["page"]
    page.goto(f"{studio}/#{hash_}")
    page.reload()
    page.wait_for_function("() => typeof D !== 'undefined' && D && D.assets && D.assets.length > 0")
    page.wait_for_timeout(300)
    shown = page.evaluate("roots => roots.filter(id => getComputedStyle(document.getElementById(id)).display !== 'none')", ROOTS)
    assert shown == [root], shown


def test_tab_order_never_reaches_a_hidden_view(studio_page):
    page = studio_page["page"]
    go(page, "game")
    go(page, "activities")
    page.locator("main").focus()
    owners = set()
    for _ in range(40):
        page.keyboard.press("Tab")
        owners.add(page.evaluate("() => document.activeElement.closest('#detail, #sheet, #creature, #behaviour, #game, #activities')?.id || ''"))
    assert owners - {""} <= {"activities"}, owners


# ---------- C2a: Activities registers with the shell's dirty tracking ----------

def edit_activity(page, value="45"):
    go(page, "activities")
    field = page.locator("#activities input[data-field=duration_s]")
    field.fill(value)
    field.press("Tab")


def test_activities_edit_shows_in_the_unsaved_indicator(studio_page):
    page = studio_page["page"]
    assert unsaved_button(page).inner_text() == ""
    edit_activity(page)
    page.wait_for_function("() => document.getElementById('shell-unsaved').textContent.includes('Activities')")
    assert not page.locator("#tab-activities .shell-tab-dirty").is_hidden()
    assert page.evaluate("() => Studio.clipsDirty()")  # the page's beforeunload guard


# ---------- C2b: drafts ----------

def test_creature_clip_edit_comes_back_after_a_reload(studio_page):
    page = studio_page["page"]
    go(page, "creature")
    page.evaluate("() => { state.cForm = 'axolotl'; state.cPose = 'idle'; Studio.cr.rerender(); Studio.cr.change(w => { w.durations_ms[0] = 321; }); }")
    wait_draft(page, "clips")
    page = studio_page["reopen"]()
    toast(page, "Restored unsaved clip edits").wait_for()
    assert page.evaluate("() => Studio.cr.current('axolotl.idle').durations_ms[0]") == 321
    page.wait_for_function("() => document.getElementById('shell-unsaved').textContent.includes('Creature clips')")


def edit_clip(page):
    go(page, "creature")
    page.evaluate("() => { state.cForm = 'axolotl'; state.cPose = 'idle'; Studio.cr.rerender(); Studio.cr.change(w => { w.durations_ms[0] = 321; }); }")


def test_clip_save_sends_the_clip_base(studio_page):
    page = studio_page["page"]
    sent = []

    def route(r):  # answered here, so the session-wide fixture keeps its clips
        sent.append(r.request.post_data_json)
        r.fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "changed": ["axolotl.idle"], "version": "v2", "clip_shas": {"axolotl.idle": "new"}}))

    page.route("**/api/clips", route)
    edit_clip(page)
    page.locator("#cr-save").click()
    expect(page.locator("#cr-save-status")).to_have_text("Saved")
    expect(page.locator("#cr-save")).to_have_text("Save clips")
    expect(page.locator("#cr-save")).to_be_disabled()
    assert sent[0]["bases"].keys() == {"axolotl.idle"}
    assert sent[0]["bases"]["axolotl.idle"]  # the sha the page loaded, not missing: the server can answer 409


def test_clip_edit_over_a_clip_that_moved_on_disk_asks_and_cancel_keeps_both(studio_page):
    page = studio_page["page"]
    edit_clip(page)
    # Another editor moved the clip: the page still holds the sha the edit started from.
    page.route("**/api/clips", lambda r: r.fulfill(status=409, content_type="application/json", body=json.dumps(
        {"error": "Clip axolotl.idle changed since you loaded it; reload to see the new version", "stale": True, "current": {"axolotl.idle": "newer"}})))
    page.locator("#cr-save").click()
    dialog(page).wait_for()
    dialog(page).get_by_role("button", name="Cancel").click()
    assert page.evaluate("() => Studio.cr.current('axolotl.idle').durations_ms[0]") == 321  # edits kept
    page.wait_for_function("() => document.getElementById('shell-unsaved').textContent.includes('Creature clips')")


def test_clip_draft_for_a_clip_that_changed_on_disk_is_restored_with_its_old_base(studio_page):
    page = studio_page["page"]
    edit_clip(page)
    wait_draft(page, "clips")
    page.evaluate("""() => { const k = 'jelli-art:draft:clips:clips', d = JSON.parse(localStorage.getItem(k));
        d.data.shas['axolotl.idle'] = 'old'; localStorage.setItem(k, JSON.stringify(d)); }""")
    page = studio_page["reopen"]()
    toast(page, "changed on disk since").wait_for()
    assert page.evaluate("() => Studio.cr.current('axolotl.idle').durations_ms[0]") == 321  # not lost
    go(page, "creature")
    seen = []
    page.route("**/api/clips", lambda r: (seen.append(r.request.post_data_json), r.continue_()))
    page.locator("#cr-save").click()
    dialog(page).wait_for()  # the real server answered 409: the base was the old sha
    assert seen[0]["bases"] == {"axolotl.idle": "old"}
    dialog(page).get_by_role("button", name="Cancel").click()


def size_edit(page):
    """Move the Creature tab's actor scale one step; returns (before, after)."""
    go(page, "creature")
    scale = page.locator("#bh-scale")
    before = int(scale.input_value())
    values = [int(v) for v in scale.locator("option").all_text_contents()]
    after = before + 1 if before + 1 in values else before - 1
    scale.select_option(str(after))
    return before, after


def test_behaviour_and_size_edit_comes_back_after_a_reload(studio_page):
    page = studio_page["page"]
    before, after = size_edit(page)
    wait_draft(page, "creatures")
    page = studio_page["reopen"]()
    toast(page, "Restored unsaved behaviour and size edits").wait_for()
    go(page, "creature")
    assert int(page.locator("#bh-scale").input_value()) == after != before
    assert not page.locator("#bh-revert").is_disabled()


def behaviour_edit(page, value="77"):
    """Set the first state's cooldown in the Behaviour view (editing needs cmake, which the test server lacks)."""
    go(page, "behaviour")
    page.evaluate("() => { D.behavior_editable = true; renderView(); }")
    field = page.locator('[data-b="states.0.cooldown_s"]')
    field.fill(value)
    field.press("Tab")
    return field


def test_behaviour_edit_comes_back_after_a_reload(studio_page):
    page = studio_page["page"]
    behaviour_edit(page)
    wait_draft(page, "behaviour")
    page = studio_page["reopen"]()
    toast(page, "Restored unsaved behaviour edits").wait_for()
    go(page, "behaviour")
    assert page.locator('[data-b="states.0.cooldown_s"]').input_value() == "77"
    page.wait_for_function("() => document.getElementById('shell-unsaved').textContent.includes('Behaviour states')")


def test_activities_edit_comes_back_after_a_reload(studio_page):
    page = studio_page["page"]
    edit_activity(page, "45")
    wait_draft(page, "activities")
    page = studio_page["reopen"]()
    toast(page, "Restored unsaved activity edits").wait_for()
    go(page, "activities")
    assert page.locator("#activities input[data-field=duration_s]").input_value() == "45"
    page.wait_for_function("() => document.getElementById('shell-unsaved').textContent.includes('Activities')")


def test_a_reverted_edit_leaves_no_draft(studio_page):
    page = studio_page["page"]
    edit_activity(page, "45")
    wait_draft(page, "activities")
    page.locator("#ac-revert").click()
    dialog(page).get_by_role("button", name="Discard edits").click()
    page.wait_for_function("() => !Object.keys(localStorage).some(n => n.startsWith('jelli-art:draft:activities:'))")


# ---------- C3: Revert and Reload saved ask, and discard only their own edits ----------

def test_activities_reload_saved_asks_first(studio_page):
    page = studio_page["page"]
    edit_activity(page, "45")
    page.locator("#ac-revert").click()
    assert dialog(page).count() == 1
    dialog(page).get_by_role("button", name="Cancel").click()
    assert page.locator("#activities input[data-field=duration_s]").input_value() == "45"
    page.locator("#ac-revert").click()
    dialog(page).get_by_role("button", name="Discard edits").click()
    expect(page.locator("#activities input[data-field=duration_s]")).not_to_have_value("45")


def test_activities_reload_saved_without_edits_acts_at_once(studio_page):
    page = studio_page["page"]
    go(page, "activities")
    page.locator("#ac-revert").click()
    page.wait_for_timeout(150)
    assert dialog(page).count() == 0
    assert page.locator("#activities input[data-field=duration_s]").count() == 1


def test_behaviour_revert_asks_first(studio_page):
    page = studio_page["page"]
    field = behaviour_edit(page, "77")
    page.locator("#bv-revert").click()
    assert dialog(page).count() == 1
    dialog(page).get_by_role("button", name="Cancel").click()
    assert field.input_value() == "77"
    page.locator("#bv-revert").click()
    dialog(page).get_by_role("button", name="Discard edits").click()
    expect(page.locator('[data-b="states.0.cooldown_s"]')).not_to_have_value("77")


def test_behaviour_revert_leaves_the_creature_tabs_size_edit(studio_page):
    page = studio_page["page"]
    _, after = size_edit(page)
    behaviour_edit(page, "77")
    page.locator("#bv-revert").click()
    dialog(page).get_by_role("button", name="Discard edits").click()
    expect(page.locator('[data-b="states.0.cooldown_s"]')).not_to_have_value("77")
    go(page, "creature")
    expect(page.locator("#bh-scale")).to_have_value(str(after))
    assert "Behaviour & size" in unsaved_button(page).inner_text()


def test_size_revert_asks_first_and_leaves_looks_alone(studio_page):
    page = studio_page["page"]
    before, after = size_edit(page)
    go(page, "behaviour")
    page.evaluate("() => { D.behavior_editable = true; renderView(); }")
    caption = page.locator('[data-c$=".caption"]').first
    caption.fill("ZZZ")
    caption.press("Tab")
    go(page, "creature")
    page.locator("#bh-revert").click()
    assert dialog(page).count() == 1
    dialog(page).get_by_role("button", name="Cancel").click()
    assert int(page.locator("#bh-scale").input_value()) == after
    page.locator("#bh-revert").click()
    dialog(page).get_by_role("button", name="Discard edits").click()
    expect(page.locator("#bh-scale")).to_have_value(str(before))
    go(page, "behaviour")
    expect(page.locator('[data-c$=".caption"]').first).to_have_value("ZZZ")


def test_size_revert_without_edits_is_disabled(studio_page):
    page = studio_page["page"]
    go(page, "creature")
    assert page.locator("#bh-revert").is_disabled()


# ---------- C4: Test in game says what it renders ----------

CATALOGUE = {"available": True, "pages": [{"name": "home", "ring": False}], "entries": [{"name": "MINT", "forms": [{"name": "Mint"}]}],
             "states": [], "moments": [], "needs": ["satiety", "energy", "hygiene", "amusement", "social"],
             "limits": {"start_ms": 5000, "frames": 24, "step_ms": 2000, "advance_s": 3600}}
RENDERED = {"ok": True, "rebuilt": True, "frames": [PIXEL], "alt": ["frame"], "times_ms": [0], "scenario": {"step_ms": 200},
            "engine": {"form_name": "Mint", "pose": 0, "behavior": "", "asleep": False, "activity": "none", "moment": 0, "mess": False,
                       "clock_minute": 540, "night": False, "needs": [700, 700, 700, 700, 700], "potty": 0}}


def mock_engine(page):
    def handler(route):
        body = CATALOGUE if route.request.method == "GET" else RENDERED
        route.fulfill(status=200, content_type="application/json", body=json.dumps(body))

    page.route("**/api/game-preview", handler)


def render_text(page):
    page.locator("#gp-render").click()
    page.wait_for_function("() => /^Rendered/.test(document.getElementById('gp-status').textContent)")
    return page.locator("#gp-status").inner_text()


def test_game_says_it_renders_the_saved_files_when_edits_are_unsaved(studio_page):
    page = studio_page["page"]
    mock_engine(page)
    go(page, "creature")
    page.evaluate("() => { state.cForm = 'axolotl'; state.cPose = 'idle'; Studio.cr.rerender(); Studio.cr.change(w => { w.durations_ms[0] = 321; }); }")
    go(page, "game")
    note = page.locator("#gp-unsaved")
    note.wait_for()
    text = note.inner_text()
    assert "saved files" in text and "1 unsaved edit" in text and "Creature clips" in text, text
    box = note.bounding_box()
    assert box["y"] + box["height"] <= page.viewport_size["height"], "the note is below the fold"
    near = page.locator("#gp-form .gp-actions #gp-unsaved-near")
    near.wait_for()
    assert "not your 1 unsaved edit" in near.inner_text()
    assert abs(near.bounding_box()["y"] - page.locator("#gp-render").bounding_box()["y"]) < 100
    status = render_text(page)
    assert "from the saved files" in status and "1 unsaved edit" in status and "Creature clips" in status, status
    assert "rebuilt from your changes" not in status


def test_game_keeps_its_message_when_nothing_is_unsaved(studio_page):
    page = studio_page["page"]
    mock_engine(page)
    go(page, "game")
    page.locator("#gp-render").wait_for()
    assert page.locator("#gp-unsaved, #gp-unsaved-near").count() == 0
    assert render_text(page).endswith("(engine rebuilt from your changes).")
