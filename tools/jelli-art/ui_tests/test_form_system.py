"""Shared persistent form layout, focus and motion without enlarging editor controls."""
from pathlib import Path

import pytest
from playwright.sync_api import expect

from conftest import new_view
from test_creation import creator  # noqa: F401 - copied checkout with all editor documents


def open_mode(page, mode, selector):
    page.evaluate("mode => JelliShell.activateMode(mode)", mode)
    expect(page.locator(selector).first).to_be_visible()


def geometry(page, label, control):
    return page.evaluate("""([label, control]) => {
      const a = document.querySelector(label).getBoundingClientRect();
      const b = document.querySelector(control).getBoundingClientRect();
      return {label: {left:a.left, right:a.right, top:a.top, bottom:a.bottom},
              control: {left:b.left, right:b.right, top:b.top, bottom:b.bottom,
                        width:b.width, height:b.height}};
    }""", [label, control])


def capture(page, name):
    out = Path("build/ui-tests/screenshots")
    out.mkdir(parents=True, exist_ok=True)
    page.screenshot(path=str(out / name), animations="disabled")


@pytest.mark.parametrize("width,height", [(1440, 900), (820, 1180), (390, 844), (320, 568)])
def test_new_form_wraps_at_its_own_width(creator, width, height):
    page = creator["page"]
    page.set_viewport_size({"width": width, "height": height})
    form = page.locator("#creation-form")
    expect(form).to_be_visible()
    create = geometry(page, "#creation-form label:has([name=kind])", "#creation-form [name=kind]")
    identity = geometry(page, "#creation-form label:has([name=key])", "#creation-form [name=key]")
    assert identity["control"]["top"] >= identity["label"]["top"]
    assert abs(identity["control"]["left"] - identity["label"]["left"]) <= 1
    assert abs(identity["control"]["right"] - identity["label"]["right"]) <= 1
    if form.bounding_box()["width"] >= 560:
        assert abs(create["label"]["top"] - identity["label"]["top"]) <= 1
    else:
        assert identity["label"]["top"] > create["control"]["bottom"]
    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
    if width in (1440, 390):
        capture(page, f"form-new-{width}.png")


@pytest.mark.parametrize("width,height", [(1440, 900), (390, 844)])
def test_persistent_editor_fields_fit_their_labels(creator, width, height):
    page = creator["page"]
    page.set_viewport_size({"width": width, "height": height})
    cases = [
        ("activities", "#activities .ac-fields", "#activities label:has([data-field=duration_s])", "#activities [data-field=duration_s]"),
        ("scenes", "#scene-name", "#scenes label:has(#scene-name)", "#scene-name"),
        ("effects", "#fx-name", "#effects label:has(#fx-name)", "#fx-name"),
        ("game", "#gp-form", "#game .gp-row:has(#gp-pet)", "#gp-pet"),
        ("music", "#mu-files", "#music label:has(#mu-title)", "#mu-title"),
    ]
    for mode, ready, label, control in cases:
        open_mode(page, mode, ready)
        if mode == "music":
            # The title is in the intentionally collapsed files disclosure.
            page.locator("#mu-files").evaluate("details => { details.open = true; }")
        box = geometry(page, label, control)
        assert box["control"]["height"] >= 38, (mode, box)
        assert box["control"]["left"] >= box["label"]["left"] - 1, (mode, box)
        assert box["control"]["right"] <= box["label"]["right"] + 1, (mode, box)
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), mode
        if width == 390 and mode == "activities":
            preview_layout = page.evaluate("""() => {
              const label = document.querySelector('#activities .ac-grid>aside label').getBoundingClientRect();
              const preview = document.querySelector('#ac-preview').getBoundingClientRect();
              const aside = document.querySelector('#activities .ac-grid>aside').getBoundingClientRect();
              return {label:{left:label.left,right:label.right,width:label.width},
                      preview:{left:preview.left,right:preview.right,width:preview.width},
                      aside:{left:aside.left,right:aside.right,width:aside.width}};
            }""")
            assert preview_layout["label"]["width"] >= 100, preview_layout
            assert preview_layout["label"]["right"] + 8 <= preview_layout["preview"]["left"], preview_layout
            assert preview_layout["preview"]["right"] <= preview_layout["aside"]["right"] + 1, preview_layout
            capture(page, "form-activities-390.png")


def test_form_focus_motion_and_compact_controls(creator):
    page = creator["page"]
    field = page.locator("#creation-form [name=key]")
    page.emulate_media(reduced_motion="no-preference")
    assert page.evaluate("getComputedStyle(document.querySelector('#creation-form [name=key]')).transitionDuration") == "0.14s, 0.14s"
    page.mouse.move(0, 0)
    page.wait_for_timeout(180)
    normal = field.evaluate("el => getComputedStyle(el).borderColor")
    field.hover()
    page.wait_for_timeout(180)
    assert field.evaluate("el => getComputedStyle(el).borderColor") != normal
    field.focus()
    assert page.evaluate("getComputedStyle(document.querySelector('#creation-form [name=key]')).outlineWidth") == "2px"
    page.emulate_media(reduced_motion="reduce")
    assert page.evaluate("getComputedStyle(document.querySelector('#creation-form [name=key]')).transitionDuration") == "0s"
    open_mode(page, "paint", "#tools button:first-child")
    assert page.evaluate("parseFloat(getComputedStyle(document.querySelector('#tools button')).minHeight)") == 36
    open_mode(page, "music", "#mu-grid td button")
    assert page.evaluate("parseFloat(getComputedStyle(document.querySelector('#mu-grid td button')).minHeight)") == 32
    open_mode(page, "effects", "#fx-timeline")
    assert page.evaluate("parseFloat(getComputedStyle(document.querySelector('.fx-bar')).height)") == 28


@pytest.mark.parametrize("viewport", ["ipad-portrait", "ipad-landscape"])
def test_new_and_activities_fit_touch_viewports(browser, studio, viewport):
    context, page = new_view(browser, studio, "paint-creature", viewport)
    try:
        for mode, root in (("creation", "#creation-form"), ("activities", "#activities")):
            open_mode(page, mode, root)
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), (mode, viewport)
            small = page.evaluate("""root => [...document.querySelectorAll(`${root} button, ${root} select, ${root} input, ${root} textarea`)]
              .filter(el => el.getBoundingClientRect().width && !el.closest('[hidden]'))
              .filter(el => ((el.type === 'checkbox' || el.type === 'radio') ?
                  el.closest('label')?.getBoundingClientRect().height : el.getBoundingClientRect().height) < 43.5)
              .map(el => `${el.tagName}#${el.id}: ${el.getBoundingClientRect().height}`)""", root)
            assert not small, (mode, viewport, small)
        if viewport == "ipad-portrait":
            layout = page.evaluate("""() => {
              const aside = document.querySelector('#activities .ac-grid>aside').getBoundingClientRect();
              const label = document.querySelector('#activities .ac-grid>aside label').getBoundingClientRect();
              const canvas = document.querySelector('#ac-preview').getBoundingClientRect();
              return {aside:aside.width,label:label.width,gap:canvas.left-label.right};
            }""")
            assert layout["label"] >= 100 and layout["gap"] >= 8, layout
    finally:
        context.close()
