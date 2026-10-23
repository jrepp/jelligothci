"""Scene composition, motion, persistence, and continued editing during saves."""
import json
from pathlib import Path

import pytest
from playwright.sync_api import expect

from test_creation import creator  # noqa: F401 - real copied-checkout fixture


def open_scenes(page):
    page.evaluate("JelliShell.activateMode('scenes')")
    expect(page.locator("#scene-canvas")).to_be_visible()


def test_scene_motion_shared_library_and_save(creator):
    page = creator["page"]
    open_scenes(page)
    initial = page.locator("#scene-canvas").evaluate("(c)=>c.toDataURL()")
    page.locator("#scene-pet").uncheck()
    page.locator("#scene-tx").fill("1")
    page.locator("#scene-play").click()
    page.wait_for_function("() => parseFloat(document.querySelector('#scene-travel-value').textContent) > 3")
    assert initial != page.locator("#scene-canvas").evaluate("(c)=>c.toDataURL()")
    page.locator("#scene-play").click()
    page.locator("#scene-reset").click()
    expect(page.locator("#scene-travel-value")).to_have_text("0.0 px")
    page.locator("#scene-copy").click()
    page.locator("#scene-name").fill("Shared kit test")
    page.locator("#scene-library").select_option("backgrounds.kit-trees")
    page.locator("#scene-add").click()
    page.locator('[data-number="travel"]').fill("80")
    page.locator('[data-number="travel"]').press("Tab")
    expect(page.locator("#scene-repeat")).to_be_checked()
    page.locator("#scene-save").click()
    expect(page.locator("#scene-status")).to_have_text("Saved")
    doc = json.loads((creator["content"] / "scenes.json").read_text())
    assert doc["scenes"][-1]["name"] == "Shared kit test"
    assert doc["scenes"][-1]["layers"][-1]["asset"] == "backgrounds.kit-trees"
    assert doc["scenes"][-1]["layers"][-1]["travel"] == 80
    page.reload()
    open_scenes(page)
    page.locator("#scene-select").select_option(doc["scenes"][-1]["key"])
    expect(page.locator("#scene-name")).to_have_value("Shared kit test")


def test_scene_save_preserves_newer_edits(creator):
    page = creator["page"]
    open_scenes(page)
    pending = []
    page.route("**/api/scenes", lambda route: pending.append(route) if route.request.method == "POST" else route.continue_())
    page.locator("#scene-name").fill("Submitted name")
    page.locator("#scene-save").click()
    expect(page.locator("#scene-status")).to_have_text("Saving…")
    page.locator("#scene-name").fill("Newer name")
    assert len(pending) == 1
    pending[0].continue_()
    expect(page.locator("#scene-status")).to_have_text("Unsaved changes")
    expect(page.locator("#scene-name")).to_have_value("Newer name")
    doc = json.loads((creator["content"] / "scenes.json").read_text())
    assert doc["scenes"][0]["name"] == "Submitted name"
    page.unroute("**/api/scenes")
    page.locator("#scene-save").click()
    expect(page.locator("#scene-status")).to_have_text("Saved")


def test_scene_draft_and_stale_save_cancel(creator):
    page = creator["page"]
    open_scenes(page)
    page.locator("#scene-name").fill("Recover this scene")
    page.evaluate("JelliDrafts.flush()")
    page.reload()
    open_scenes(page)
    expect(page.locator("#scene-name")).to_have_value("Recover this scene")
    path = creator["content"] / "scenes.json"
    doc = json.loads(path.read_text())
    doc["scenes"][0]["name"] = "Other editor"
    path.write_text(json.dumps(doc))
    page.locator("#scene-save").click()
    dialog = page.get_by_role("dialog")
    expect(dialog).to_be_visible()
    dialog.get_by_role("button", name="Cancel", exact=True).click()
    expect(page.locator("#scene-name")).to_have_value("Recover this scene")
    assert json.loads(path.read_text())["scenes"][0]["name"] == "Other editor"


@pytest.mark.parametrize("width,height", [(1440,1000), (390,844)])
def test_scene_layout_and_visual_review(creator, width, height):
    page = creator["page"]
    page.set_viewport_size({"width":width, "height":height})
    open_scenes(page)
    expect(page.locator("#scene-canvas")).to_be_visible()
    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), page.evaluate(
        "() => JSON.stringify([...document.querySelectorAll('body *')].filter(e=>e.getBoundingClientRect().right>innerWidth).map(e=>[e.tagName,e.id,e.className,e.getBoundingClientRect().right]))")
    box = page.locator("#scene-canvas").bounding_box()
    assert box["width"] >= 240
    page.locator("#scene-canvas").scroll_into_view_if_needed()
    page.wait_for_function("() => document.querySelector('#scene-travel-value').textContent === '0.0 px'")
    out = Path("build/ui-tests/screenshots")
    out.mkdir(parents=True, exist_ok=True)
    page.screenshot(path=str(out / f"scenes-{width}.png"), full_page=True)


def test_new_background_layer_without_location(creator):
    page = creator["page"]
    form = page.locator("#creation-form")
    form.locator('[name="kind"]').select_option("backgrounds")
    form.locator('[name="key"]').fill("scene-layer")
    form.locator('[name="name"]').fill("SCENE LAYER")
    form.locator('[name="background_use"]').select_option("layer")
    before = (creator["content"] / "locations.json").read_bytes()
    form.get_by_role("button", name="Create and paint").click()
    page.wait_for_function("() => state.key === 'backgrounds.scene-layer'", timeout=30000)
    assert before == (creator["content"] / "locations.json").read_bytes()
    open_scenes(page)
    page.locator("#scene-library").select_option("backgrounds.scene-layer")
    page.locator("#scene-add").click()
    expect(page.locator("#scene-asset")).to_have_value("backgrounds.scene-layer")


def test_scene_opens_saved_engine_preview(creator):
    page = creator["page"]
    open_scenes(page)
    page.locator("#scene-select").select_option("garden-walk")
    page.get_by_role("button", name="Test saved scene in game").click()
    expect(page.locator("#gp-scene")).to_have_value("garden-walk")
    expect(page.locator('[data-k="tilt_x"]')).to_have_value("0")
    expect(page.locator('[data-k="travel_speed"]')).to_have_value("0")
    page.locator('[data-k="travel_speed"]').fill("8")
    page.locator('[data-k="travel_speed"]').press("Tab")
    page.reload()
    page.evaluate("JelliShell.activateMode('game')")
    expect(page.locator("#gp-scene")).to_have_value("garden-walk")
    expect(page.locator('[data-k="travel_speed"]')).to_have_value("8")


def test_scene_layer_names_and_seeded_variation(creator):
    page = creator["page"]
    open_scenes(page)
    page.locator('[data-layer="2"]').click()
    page.locator('#scene-layer-name').fill('Shopfronts & windows')
    expect(page.locator('[data-layer="2"]')).to_have_text('3. Shopfronts & windows')
    page.locator('#scene-variation summary').click()
    page.locator('[data-random="x"]').fill('16')
    page.locator('[data-random="x"]').press('Tab')
    page.locator('[data-random="travel"]').fill('20')
    page.locator('[data-random="travel"]').press('Tab')
    before = page.locator('#scene-resolved').inner_text()
    page.locator('#scene-reroll').click()
    expect(page.locator('#scene-seed')).to_have_value('1')
    expect(page.locator('#scene-resolved')).not_to_have_text(before)
    page.locator('[data-random="chance"]').fill('0')
    page.locator('[data-random="chance"]').press('Tab')
    expect(page.locator('#scene-resolved')).to_contain_text('not shown')
    page.locator('[data-random="chance"]').fill('100')
    page.locator('[data-random="chance"]').press('Tab')
    chosen = page.locator('#scene-resolved').inner_text()
    page.locator('#scene-back').click()
    expect(page.locator('#scene-resolved')).to_have_text(chosen)
    expect(page.locator('[data-layer="1"]')).to_have_text('2. Shopfronts & windows')
    page.locator('#scene-save').click()
    expect(page.locator('#scene-status')).to_have_text('Saved')
    doc = json.loads((creator['content'] / 'scenes.json').read_text())
    assert doc['scenes'][0]['seed'] == 1
    assert doc['scenes'][0]['layers'][1]['name'] == 'Shopfronts & windows'
    assert doc['scenes'][0]['layers'][1]['random']['x'] == 16
    page.reload()
    open_scenes(page)
    page.locator('[data-layer="1"]').click()
    expect(page.locator('#scene-layer-name')).to_have_value('Shopfronts & windows')
    page.locator('#scene-variation summary').click()
    expect(page.locator('#scene-resolved')).to_have_text(chosen)


def test_scene_duplicate_undo_redo_and_preview_isolation(creator):
    page = creator['page']
    open_scenes(page)
    page.locator('[data-layer="2"]').click()
    page.locator('#scene-duplicate').click()
    expect(page.locator('#scene-layer-name')).to_have_value('Shopfronts copy')
    expect(page.locator('[data-random="seed"]')).to_have_value('2')
    expect(page.locator('[data-layer]')).to_have_count(6)
    page.locator('#scene-remove').click()
    expect(page.locator('[data-layer]')).to_have_count(5)
    page.locator('#scene-undo').click()
    expect(page.locator('[data-layer]')).to_have_count(6)
    expect(page.locator('#scene-layer-name')).to_have_value('Shopfronts copy')
    page.locator('#scene-undo').click()
    expect(page.locator('[data-layer]')).to_have_count(5)
    expect(page.locator('#scene-status')).to_have_text('Saved')
    page.locator('#scene-redo').click()
    expect(page.locator('[data-layer]')).to_have_count(6)
    page.locator('#scene-undo').click()
    page.locator('#scene-pet').uncheck()
    before = page.locator('#scene-canvas').evaluate('(c)=>c.toDataURL()')
    page.locator('#scene-solo').click()
    expect(page.locator('#scene-solo')).to_have_attribute('aria-pressed','true')
    page.wait_for_function('(before)=>document.querySelector("#scene-canvas").toDataURL()!==before',arg=before)
    expect(page.locator('#scene-status')).to_have_text('Saved')
    page.locator('#scene-solo').click()
    expect(page.locator('#scene-solo')).to_have_attribute('aria-pressed','false')


def test_scene_save_and_test_commits_active_numeric_field(creator):
    page = creator['page']
    open_scenes(page)
    page.locator('[data-layer="2"]').click()
    page.locator('[data-number="x"]').fill('13')
    expect(page.locator('#scene-engine')).to_have_text('Save and test in game')
    page.locator('#scene-engine').click()
    expect(page.locator('#gp-scene')).to_have_value('market-street')
    doc=json.loads((creator['content']/'scenes.json').read_text())
    assert doc['scenes'][0]['layers'][2]['x']==13


def test_scene_save_and_test_preserves_edits_during_save(creator):
    page = creator['page']
    open_scenes(page)
    pending=[]
    page.route('**/api/scenes',lambda route: pending.append(route) if route.request.method=='POST' else route.continue_())
    page.locator('#scene-name').fill('Submitted')
    page.locator('#scene-engine').click()
    expect(page.locator('#scene-status')).to_have_text('Saving…')
    page.locator('#scene-name').fill('Still editing')
    pending[0].continue_()
    expect(page.locator('#scene-status')).to_have_text('Unsaved changes')
    expect(page.locator('#scene-name')).to_have_value('Still editing')
    expect(page.locator('#scene-canvas')).to_be_visible()


def test_scene_invalid_field_blocks_save(creator):
    page=creator['page']
    open_scenes(page)
    before=(creator['content']/'scenes.json').read_bytes()
    page.locator('#scene-name').fill('Valid edit')
    page.locator('[data-number="x"]').fill('999')
    page.locator('#scene-save').click()
    assert (creator['content']/'scenes.json').read_bytes()==before
    expect(page.locator('[data-number="x"]')).to_be_focused()
    expect(page.locator('#scene-status')).to_have_text('Unsaved changes')


def test_scene_keyboard_history_and_accessibility(creator):
    from test_views import AXE
    page=creator['page']
    open_scenes(page)
    count = page.locator('#scene-select option').count()
    page.locator('#scene-copy').click()
    expect(page.locator('#scene-select option')).to_have_count(count + 1)
    page.locator('#scene-copy').focus()
    page.keyboard.press('ControlOrMeta+z')
    expect(page.locator('#scene-select option')).to_have_count(count)
    page.keyboard.press('ControlOrMeta+Shift+z')
    expect(page.locator('#scene-select option')).to_have_count(count + 1)
    page.locator('[data-number="x"]').fill('13')
    page.keyboard.press('ControlOrMeta+s')
    expect(page.locator('#scene-status')).to_have_text('Saved')
    doc=json.loads((creator['content']/'scenes.json').read_text())
    assert doc['scenes'][-1]['layers'][0]['x']==13
    page.locator('#scene-variation summary').click()
    page.add_script_tag(path=str(AXE))
    violations=page.evaluate("""async () => (await axe.run(document.querySelector('#scenes'),
      {runOnly:{type:'tag',values:['wcag2a','wcag2aa']}})).violations.filter(v=>['serious','critical'].includes(v.impact))""")
    assert violations==[], violations


def test_activity_binding_and_copy(creator):
    page = creator["page"]
    open_scenes(page)
    # Edit an existing activity binding in the current copied checkout.
    page.locator("#scene-select").select_option("activity-breakfast")
    page.locator("#scene-activity-speed").fill("3")
    page.locator("#scene-activity-speed").press("Tab")
    page.locator("#scene-save").click()
    expect(page.locator("#scene-status")).to_have_text("Saved")
    doc = json.loads((creator["content"] / "scenes.json").read_text())
    bound = next(s for s in doc["scenes"] if s.get("activity") == "BREAKFAST")
    assert bound["speed"] == 3
    page.locator("#scene-copy").click()
    expect(page.locator("#scene-activity")).to_have_value("")
    page.locator("#scene-save").click()
    expect(page.locator("#scene-status")).to_have_text("Saved")
    doc = json.loads((creator["content"] / "scenes.json").read_text())
    assert "activity" not in doc["scenes"][-1]


@pytest.mark.parametrize("width,height", [(1440,1000), (390,844)])
def test_background_navigation_keeps_edits_and_layer(creator, width, height):
    page = creator["page"]
    page.set_viewport_size({"width":width,"height":height})
    open_scenes(page)
    page.locator('#scene-select').select_option('activity-reading')
    page.locator('[data-layer="2"]').click()
    page.locator('#scene-layer-name').fill('Library wall')
    page.locator('#scene-next').click()
    page.locator('#scene-prev').click()
    expect(page.locator('#scene-select')).to_have_value('activity-reading')
    expect(page.locator('#scene-layer-name')).to_have_value('Library wall')
    page.locator('#scene-search').fill('fishing')
    expect(page.locator('#scene-count')).to_contain_text('1 matching')
    page.locator('#scene-select').select_option('activity-fishing')
    page.locator('#scene-search').fill('no matching scene')
    expect(page.locator('#scene-count')).to_contain_text('No matches')
    expect(page.locator('#scene-select')).to_have_value('activity-fishing')
    page.locator('#scene-search').fill('')
    page.locator('#scene-save').click()
    expect(page.locator('#scene-status')).to_have_text('Saved')
    page.locator('#scene-reload').click()
    expect(page.locator('#scene-select')).to_have_value('activity-fishing')
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')


def test_invalid_fields_block_navigation_and_nudges_are_undoable(creator):
    page=creator['page']
    open_scenes(page)
    field=page.locator('[data-number="x"]')
    field.fill('')
    expect(page.locator('#scene-validation')).to_contain_text('Horizontal position')
    page.locator('#scene-next').click()
    expect(page.locator('#scene-select')).to_have_value('market-street')
    expect(field).to_have_value('')
    page.locator('[data-layer="1"]').click()
    expect(page.locator('[data-layer="0"]')).to_have_attribute('aria-pressed','true')
    field.fill('0')
    page.get_by_role('button',name='Move layer right one pixel',exact=True).click()
    expect(field).to_have_value('1')
    page.locator('#scene-undo').click()
    expect(field).to_have_value('0')
    page.locator('#scene-select').select_option('activity-breakfast')
    page.locator('#scene-activity-speed').fill('33')
    page.locator('#scene-next').click()
    expect(page.locator('#scene-select')).to_have_value('activity-breakfast')
    expect(page.locator('#scene-activity-speed')).to_have_value('33')


def test_shared_art_navigation_and_paint_return(creator):
    page=creator['page']
    open_scenes(page)
    page.locator('#scene-related').select_option('garden-walk')
    expect(page.locator('#scene-select')).to_have_value('garden-walk')
    expect(page.locator('#scene-asset')).to_have_value('backgrounds.kit-sky')
    expect(page.locator('#scene-art-info')).to_contain_text('Used in')
    page.locator('#scene-paint').click()
    expect(page.locator('#scene-return')).to_have_text('Back to Garden walk')
    page.locator('#scene-return').click()
    expect(page.locator('#scene-select')).to_have_value('garden-walk')
    expect(page.locator('#scene-asset')).to_have_value('backgrounds.kit-sky')


def test_background_edit_markers_and_offscreen_hint(creator):
    page=creator['page']
    open_scenes(page)
    page.locator('#scene-name').fill('Edited street')
    expect(page.locator('#scene-select option:checked')).to_contain_text('edited')
    expect(page.locator('#scene-count')).to_contain_text('1 edited')
    page.locator('#scene-select').select_option('activity-reading')
    page.locator('#scene-repeat').uncheck()
    page.locator('[data-number="x"]').fill('100')
    expect(page.locator('#scene-validation')).to_contain_text('outside the neutral preview')
    page.locator('#scene-save').click()
    expect(page.locator('#scene-status')).to_have_text('Saved')
    expect(page.locator('#scene-count')).to_contain_text('0 edited')


def test_mobile_stepper_can_repair_invalid_background_field(creator):
    page=creator['page']
    # The tap-stepper controls also exist on fine pointers, hidden by CSS.
    open_scenes(page)
    field=page.locator('[data-number="x"]')
    field.fill('')
    field.locator('..').get_by_role('button',name='Increase Horizontal position',include_hidden=True).dispatch_event('click')
    expect(field).to_have_value('1')
    page.locator('#scene-next').click()
    expect(page.locator('#scene-select')).to_have_value('garden-walk')
