"""Creative workspace: visible task controls, contextual options, focus, and nonpainting navigation."""
import json

import pytest
from playwright.sync_api import expect

from conftest import BASELINES, OUT
from test_keyboard import FOCUSED
from test_touch_paint import data, steps
from test_views import axe_check


@pytest.mark.parametrize("viewport", ["desktop", "phone"])
def test_paint_workspace_controls_fit_and_remain_keyboard_accessible(open_view, viewport):
    page = open_view("paint-creature", viewport)
    for selector in ("#paint-toolrail", "#paint-context", "#paint-commandbar"):
        expect(page.locator(selector)).to_be_visible()
        box = page.locator(selector).bounding_box()
        assert box["x"] >= -1 and box["x"] + box["width"] <= page.viewport_size["width"] + 1, selector
    for selector in ('#tools [data-tool=pencil]', '#tools [data-tool=hand]', '#save', '#undo', '#redo'):
        expect(page.locator(selector)).to_be_visible()
    expect(page.locator('#save')).to_have_text('Save')
    expect(page.locator('#save svg.jelli-icon')).to_have_count(1)
    expect(page.locator('#paint-save-status')).to_have_attribute('role', 'status')
    expect(page.locator('#paint-save-status')).to_have_text('Saved')
    hand = page.locator('#tools [data-tool=hand]')
    hand.focus()
    page.keyboard.press("Shift+Tab")
    focused = page.evaluate(FOCUSED)
    assert focused and focused["visible"], "Tool navigation needs a visible keyboard focus indicator"
    # Save reviewable evidence at both widths, without rewriting platform-specific reference PNGs.
    (OUT / "screenshots").mkdir(parents=True, exist_ok=True)
    page.screenshot(path=str(OUT / "screenshots" / f"paint-workspace@{viewport}.png"), animations="disabled")
    canvas = page.locator('#stage .paint-canvas canvas').bounding_box()
    visible_height = max(0, min(canvas['y'] + canvas['height'], page.viewport_size['height']) - max(canvas['y'], 0))
    assert visible_height >= page.viewport_size['height'] * .25, 'The fitted canvas must occupy at least a quarter of the visible viewport'
    axe_check(page, f"paint-creature@{viewport}", json.loads((BASELINES / "axe.json").read_text()))


def test_paint_options_follow_the_selected_tool_and_selection(open_view):
    page = open_view("paint-creature", "desktop")
    page.locator('#tools [data-tool=rect]').click()
    expect(page.locator('#paint-context #filled')).to_be_visible()
    expect(page.locator('#paint-context #perfect')).to_be_hidden()
    previous = page.locator('#filled').get_attribute('aria-pressed')
    page.locator('#filled').click()
    expect(page.locator('#filled')).to_have_attribute('aria-pressed', 'false' if previous == 'true' else 'true')
    page.locator('#tools [data-tool=pencil]').click()
    expect(page.locator('#paint-context #filled')).to_be_hidden()
    expect(page.locator('#paint-context #perfect')).to_be_visible()
    expect(page.locator('#paint-selection-context')).to_be_hidden()
    page.locator('#tools [data-tool=select]').click()
    expect(page.locator('#paint-selection-context')).to_be_visible()
    page.locator('#sel-all').click()
    page.locator('#tools [data-tool=pencil]').click()
    expect(page.locator('#paint-selection-context')).to_be_visible()
    page.locator('#sel-none').click()
    expect(page.locator('#paint-selection-context')).to_be_hidden()
    page.locator('#tools [data-tool=hand]').click()
    expect(page.locator('#mirror')).to_be_disabled()


def test_hand_pan_changes_view_without_painting_and_tools_survive_rebuild(open_view):
    page = open_view("paint-creature", "desktop")
    page.evaluate("() => setZoom(24)")
    page.locator('#stage .paint-canvas canvas').focus()
    page.keyboard.press('h')
    page.wait_for_function("() => state.tool === 'hand'")
    scroller = page.locator('#stage .paint-canvas .scroller')
    scroller.evaluate("el => {el.scrollLeft=100; el.scrollTop=100;}")
    before = scroller.evaluate("el=>[el.scrollLeft,el.scrollTop]")
    pixels, history = data(page), steps(page)
    box = scroller.bounding_box()
    x, y = box['x'] + box['width'] * .5, box['y'] + box['height'] * .5
    page.mouse.move(x, y)
    page.mouse.down()
    page.mouse.move(x - 45, y - 35, steps=5)
    page.mouse.up()
    after = scroller.evaluate("el=>[el.scrollLeft,el.scrollTop]")
    assert after != before, "Hand should pan the magnified artwork"
    assert data(page) == pixels and steps(page) == history, "Hand must not paint or create undo history"
    page.evaluate("() => setZoom(12)")
    page.evaluate("() => select('creatures.axolotl-idle-1')")
    for selector in ('#paint-toolrail', '#paint-context', '#paint-commandbar'):
        expect(page.locator(selector)).to_have_count(1)
        expect(page.locator(selector)).to_be_visible()
    expect(page.locator('#tools [data-tool=hand]')).to_have_attribute('aria-pressed', 'true')


def test_fine_pointer_phone_asset_drawer_closes_and_restores_focus(open_view):
    page = open_view('paint-creature', 'phone')
    assert not page.evaluate("() => matchMedia('(pointer: coarse)').matches")
    button = page.locator('#jt-list-button')
    expect(button).to_be_visible()
    expect(page.locator('aside')).to_be_hidden()
    button.click()
    expect(button).to_have_attribute('aria-expanded', 'true')
    assert page.evaluate("() => document.querySelector('aside').contains(document.activeElement)")
    page.keyboard.press('Escape')
    expect(page.locator('aside')).to_be_hidden()
    expect(button).to_be_focused()
    button.click()
    page.locator('#list .item').filter(has=page.locator('.name', has_text='axolotl-idle-1')).click()
    expect(page.locator('aside')).to_be_hidden()
    expect(button).to_have_attribute('aria-expanded', 'false')
    page.wait_for_function("() => state.key === 'creatures.axolotl-idle-1'")


def test_phone_mode_overflow_has_visible_navigation_controls(open_view):
    page = open_view('paint-creature', 'phone')
    more = page.get_by_role('button', name='More modes', exact=True)
    expect(more).to_be_visible()
    more.click()
    page.wait_for_function("() => document.querySelector('#shell-modes [role=tablist]').scrollLeft > 0")
    page.get_by_role('tab', name='New', exact=True).click()
    expect(page.locator('#creation-form')).to_be_visible()
    expect(page.get_by_role('tab', name='New', exact=True)).to_have_attribute('aria-selected', 'true')


@pytest.mark.parametrize("viewport", ["desktop", "tablet", "phone"])
def test_palette_chip_name_includes_visible_usage(open_view, viewport):
    page = open_view("paint-icon", viewport)
    chip = page.locator('div[data-chip="mint jelly:#43bca2"]')
    expect(chip).to_have_accessible_name("sea 36 px · #43bca2 · used by 34 assets")
    # The visible count is part of the name, not hidden from accessibility checks.
    expect(chip).to_contain_text("36 px")
    axe_check(page, f"paint-icon@{viewport}", json.loads((BASELINES / "axe.json").read_text()))
