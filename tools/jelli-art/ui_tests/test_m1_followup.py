"""UX M1 follow-up: drafts across several tabs (F1), a failed draft write is retried (F6), a restored stale draft can
be saved after the Overwrite prompt (F2), and each view's status and Save cover only its own section of
creatures.json (F3).

Run: tools/jelli-art/ui_tests/run.sh -k m1_followup
"""
import json

from playwright.sync_api import expect

from conftest import settle
from test_lost_work import (behaviour_edit, dialog, edit_activity, go, size_edit, studio_page,  # noqa: F401
                            toast, unsaved_button, wait_draft)

DRAFT = "jelli-art:draft:activities:activities"


def has_draft(page, key=DRAFT):
    return page.evaluate("k => localStorage.getItem(k) !== null", key)


def second_tab(studio_page, studio):
    """Another tab of the same browser context, settled on the same view (drafts are shared through localStorage)."""
    return settle(studio_page["page"].context, studio, "review-detail", "desktop")


# ---------- F1: drafts and several tabs ----------

def test_a_second_tab_does_not_restore_or_discard_a_live_tabs_draft(studio_page, studio):
    a = studio_page["page"]
    edit_activity(a, "45")
    wait_draft(a, "activities")
    b = second_tab(studio_page, studio)
    b.wait_for_timeout(1000)  # past the first tracking pass of the new tab
    assert toast(b, "Restored unsaved activity edits").count() == 0
    assert b.evaluate("() => JelliDrafts.restore('activities', 'activities', D.activity_sha)") is None
    assert b.evaluate("() => document.getElementById('shell-unsaved').textContent") == ""
    go(b, "activities")
    assert b.locator("#activities input[data-field=duration_s]").input_value() != "45"
    b.evaluate("() => JelliDrafts.drop('activities', 'activities')")  # what B's Discard would do
    assert has_draft(b), "the live tab's draft must survive another tab's discard"


def test_a_live_owner_keeps_its_claim_for_longer_than_the_heartbeat_window(studio_page, studio):
    a = studio_page["page"]
    edit_activity(a, "45")
    wait_draft(a, "activities")
    a.wait_for_timeout(6500)  # the draft was written once; the heartbeat must have kept it alive
    b = second_tab(studio_page, studio)
    assert b.evaluate("() => JelliDrafts.restore('activities', 'activities', D.activity_sha)") is None


def test_a_closed_or_dead_owner_hands_its_draft_over(studio_page, studio):
    a = studio_page["page"]
    edit_activity(a, "45")
    wait_draft(a, "activities")
    b = second_tab(studio_page, studio)
    assert b.evaluate("() => JelliDrafts.restore('activities', 'activities', D.activity_sha)") is None
    # A crashed or evicted tab never says goodbye: its heartbeat just stops.
    b.evaluate("""() => { const k = 'jelli-art:draft:activities:activities', d = JSON.parse(localStorage.getItem(k));
        d.beat = Date.now() - 6000; localStorage.setItem(k, JSON.stringify(d)); }""")
    assert b.evaluate("() => JelliDrafts.restore('activities', 'activities', D.activity_sha).data.moments[0].duration_s") == 45
    b.wait_for_timeout(1500)  # a tab that booted clean never drops a draft it did not write
    assert has_draft(b)
    a.close()  # a closed tab releases its draft at once
    c = second_tab(studio_page, studio)
    toast(c, "Restored unsaved activity edits").wait_for()


def test_a_live_owner_rewrites_a_draft_another_tab_removed(studio_page):
    a = studio_page["page"]
    edit_activity(a, "45")
    wait_draft(a, "activities")
    a.evaluate("k => localStorage.removeItem(k)", DRAFT)  # what another tab's removal looks like from storage
    a.wait_for_function("k => localStorage.getItem(k) !== null", arg=DRAFT, timeout=4000)  # no further edit was made
    assert a.evaluate("k => JSON.parse(localStorage.getItem(k)).data.moments[0].duration_s", DRAFT) == 45


# ---------- F6: a failed draft write is retried ----------

def test_a_failed_draft_write_is_retried_without_another_edit(studio_page):
    page = studio_page["page"]
    page.evaluate("""() => { window.__full = true; const set = Storage.prototype.setItem;
        Storage.prototype.setItem = function (k, v) { if (window.__full && k.startsWith('jelli-art:draft:')) throw new DOMException('full', 'QuotaExceededError'); return set.call(this, k, v); }; }""")
    edit_activity(page, "45")
    page.wait_for_timeout(1800)
    assert not has_draft(page)
    page.evaluate("() => { window.__full = false; }")
    page.wait_for_function("k => localStorage.getItem(k) !== null", arg=DRAFT, timeout=4000)


# ---------- F2: a restored stale draft can be saved, after the Overwrite prompt ----------

def stale_draft(page, key, base, bases=None):
    page.evaluate("""([k, base, bases]) => { const n = 'jelli-art:draft:' + k, d = JSON.parse(localStorage.getItem(n));
        d.base = base; if (bases) d.data.bases = bases; d.beat = 0; localStorage.setItem(n, JSON.stringify(d)); }""", [key, base, bases])


def conflict(route, body, seen):
    """The first POST answers 409 like the server; the retry (after Overwrite) succeeds."""
    seen.append(route.request.post_data_json)
    if len(seen) == 1:
        route.fulfill(status=409, content_type="application/json", body=json.dumps(
            {"error": "content changed since you loaded it; revert to load the new version", "stale": True,
             "current": {"activities": "newer", "creatures": "newer", "behaviors": "newer"}}))
    else:
        route.fulfill(content_type="application/json", body=json.dumps(body))


def test_stale_activities_draft_saves_after_an_overwrite_prompt(studio_page):
    page = studio_page["page"]
    edit_activity(page, "45")
    wait_draft(page, "activities")
    stale_draft(page, "activities:activities", "old")
    page = studio_page["reopen"]()
    toast(page, "changed on disk since").wait_for()
    assert toast(page, "Save asks before it overwrites").count() == 1  # the hint says what the button really does
    go(page, "activities")
    page.evaluate("() => { D.behavior_editable = true; renderView(); }")  # the test server has no cmake
    assert not page.locator("#ac-save").is_disabled()
    seen = []
    page.route("**/api/content", lambda r: conflict(r, {"ok": True, "changed": ["activities"], "version": "v2", "shas": {"activities": "fresh"}}, seen))
    page.locator("#ac-save").click()
    dialog(page).get_by_role("button", name="Cancel").click()
    assert len(seen) == 1 and seen[0]["bases"] == {"activities": "old"}
    assert page.locator("#activities input[data-field=duration_s]").input_value() == "45"  # Cancel keeps the edits
    seen.clear()  # the next Save is conflicted again
    page.locator("#ac-save").click()
    dialog(page).get_by_role("button", name="Overwrite").click()
    page.wait_for_function("() => document.getElementById('ac-save').disabled")
    assert seen[-1]["bases"] == {"activities": "newer"}


def test_stale_size_draft_saves_after_an_overwrite_prompt(studio_page):
    page = studio_page["page"]
    size_edit(page)
    wait_draft(page, "creatures")
    stale_draft(page, "creatures:creatures", "old")
    page = studio_page["reopen"]()
    toast(page, "changed on disk since").wait_for()
    go(page, "creature")
    assert not page.locator("#bh-save").is_disabled()
    seen = []
    page.route("**/api/creatures", lambda r: conflict(r, {"ok": True, "changed": True, "version": "v2", "sha": "fresh"}, seen))
    page.locator("#bh-save").click()
    dialog(page).get_by_role("button", name="Overwrite").click()
    page.wait_for_function("() => document.getElementById('bh-save').disabled")
    assert seen[0]["base"] == "old" and seen[1]["base"] == "newer"


def test_stale_behaviour_draft_saves_after_an_overwrite_prompt(studio_page):
    page = studio_page["page"]
    behaviour_edit(page, "77")
    wait_draft(page, "behaviour")
    stale_draft(page, "behaviour:content", "old:old", {"b": "old", "c": "old"})
    page = studio_page["reopen"]()
    toast(page, "changed on disk since").wait_for()
    go(page, "behaviour")
    page.evaluate("() => { D.behavior_editable = true; renderView(); }")
    assert not page.locator("#bv-save").is_disabled()
    seen = []
    page.route("**/api/content", lambda r: conflict(r, {"ok": True, "changed": ["behaviors"], "version": "v2",
                                                         "shas": {"behaviors": "fresh", "creatures": "fresh"}}, seen))
    page.locator("#bv-save").click()
    dialog(page).get_by_role("button", name="Overwrite").click()
    page.wait_for_function("() => document.getElementById('bv-save').disabled")
    assert seen[0]["bases"]["behaviors"] == "old" and seen[1]["bases"]["behaviors"] == "newer"


# ---------- F3: each view's status and Save cover only its own section of creatures.json ----------

def look_edit(page):
    go(page, "behaviour")
    page.evaluate("() => { D.behavior_editable = true; renderView(); }")
    caption = page.locator('[data-c$=".caption"]').first
    caption.fill("ZZZ")
    caption.press("Tab")


def test_behaviour_view_ignores_the_creature_tabs_size_edit(studio_page):
    page = studio_page["page"]
    size_edit(page)
    go(page, "behaviour")
    page.evaluate("() => { D.behavior_editable = true; renderView(); }")
    expect(page.locator("#behaviour .cr-msgs .ok")).to_have_text("No unsaved behaviour edits.")
    assert page.locator("#bv-save").is_disabled()
    assert page.locator("#bv-save").inner_text() == "Saved"


def test_size_view_ignores_the_behaviour_views_look_edit(studio_page):
    page = studio_page["page"]
    look_edit(page)
    go(page, "creature")
    expect(page.locator("#cr-behaviour .cr-msgs .ok")).to_have_text("No unsaved behaviour or size edits.")
    assert page.locator("#bh-save").is_disabled()


def test_size_save_writes_only_the_size_section(studio_page):
    page = studio_page["page"]
    look_edit(page)
    _, after = size_edit(page)
    saved_looks = page.evaluate("() => D.creature_data.state_presentation")
    seen = []

    def route(r):
        seen.append(r.request.post_data_json)
        r.fulfill(content_type="application/json", body=json.dumps({"ok": True, "changed": True, "version": "v2", "sha": "fresh"}))

    page.route("**/api/creatures", route)
    page.locator("#bh-save").click()
    page.wait_for_function("() => document.getElementById('bh-save').disabled")
    assert seen[0]["data"]["state_presentation"] == saved_looks  # the unsaved look is not in the file
    assert any(p["scale"] == after for p in seen[0]["data"]["profiles"])
    assert "Behaviour states" in unsaved_button(page).inner_text()  # the look is still waiting in its own view
    page.wait_for_function("() => !document.getElementById('shell-unsaved').textContent.includes('Behaviour & size')")


def test_behaviour_save_writes_only_the_looks_section(studio_page):
    page = studio_page["page"]
    size_edit(page)
    look_edit(page)
    saved_profiles = page.evaluate("() => D.creature_data.profiles")
    seen = []

    def route(r):
        seen.append(r.request.post_data_json)
        r.fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "changed": ["creatures"], "version": "v2", "shas": {"creatures": "fresh", "behaviors": "x"}}))

    page.route("**/api/content", route)
    page.locator("#bv-save").click()
    page.wait_for_function("() => document.getElementById('bv-save').disabled")
    docs = seen[0]["docs"]
    assert list(docs) == ["creatures"]  # behaviors.json was not touched
    assert docs["creatures"]["profiles"] == saved_profiles
    assert docs["creatures"]["state_presentation"][0]["caption"] == "ZZZ"
    assert "Behaviour & size" in unsaved_button(page).inner_text()
    page.wait_for_function("() => !document.getElementById('shell-unsaved').textContent.includes('Behaviour states')")
