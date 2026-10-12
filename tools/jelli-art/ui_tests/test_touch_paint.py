"""RFC-007 T1: Paint with a Pencil and fingers. Pen and finger routing, the Pencil only switch, palm and
second-finger arbitration, pinch and pan, the two- and three-finger taps, long-press pick, the quick bar and the
colour menu, driven with emulated Pencil and finger input (touch.py) at the iPad sizes.

Emulation is not iPadOS Safari: palm rejection, Pencil hover and the feel of the gestures are on the physical checklist.

Run: tools/jelli-art/ui_tests/run.sh -k touch_paint
"""
import json

import pytest

from touch import (drag, long_press, pan, pen_down, pen_move, pen_stroke, pen_up, pinch, small_targets, tap, thresholds,
                   touch_end, touch_start)

VIEW = "paint-creature"
LAND, PORT = "ipad-landscape", "ipad-portrait"


def ready(page):
    """The canvas in view, at the zoom the page chose (the fit; UX M2-B closed the blind spot of forcing 4x)."""
    page.locator("#stage .paint-canvas canvas").scroll_into_view_if_needed()
    return page


@pytest.fixture
def paint(open_view):
    """paint(size=LAND, pencil_only=False) -> a Paint page at an iPad size with a fresh viewer."""
    def make(size=LAND, pencil_only=False):
        page = open_view(VIEW, size)
        ready(page)
        if pencil_only:
            page.evaluate("() => JelliTouch.setPencilOnly(true)")
        return page
    yield make


# ---------- helpers ----------

def canvas(page):
    """The paint canvas box in viewport px, its zoom and the sprite size."""
    return page.evaluate("""() => { const c = document.querySelector('#stage .paint-canvas canvas'), r = c.getBoundingClientRect(), a = byKey[state.key];
        return {x: r.left, y: r.top, z: r.width / a.width, w: a.width, h: a.height}; }""")


def screen(page, px, py):
    c = canvas(page)
    return (c["x"] + (px + 0.5) * c["z"], c["y"] + (py + 0.5) * c["z"])


def pixel_at(page, px, py):
    return page.evaluate("([x, y]) => pixel(decoded[state.key].after, x, y)", [px, py])


def blank_run(page, n=6, skip=0):
    """(x, y) of n consecutive transparent pixels in a row, the skip-th such run from the top."""
    return page.evaluate("""([n, skip]) => { const p = decoded[state.key].after; let seen = 0;
        for (let y = 2; y < p.h - 2; y++) for (let x = 2; x + n < p.w - 2; x++) {
          let ok = true; for (let i = 0; i < n && ok; i++) ok = pixel(p, x + i, y) === null;
          if (ok && seen++ === skip) return [x, y]; }
        return null; }""", [n, skip])


def data(page):
    return page.evaluate("() => Array.from(decoded[state.key].after.data)")


def steps(page):
    return page.locator("#paint-history li").count()


def current_step(page):
    return int(page.locator("#paint-history [aria-current=step]").get_attribute("data-step"))


def centre(page, selector):
    target = page.locator(selector).first
    target.scroll_into_view_if_needed()
    box = target.bounding_box()
    return (box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)


def row_stroke(page, x, y, n=4):
    """A Pencil stroke along n pixels of a row."""
    pen_stroke(page, [screen(page, x, y), screen(page, x + n - 1, y)])


def state_of(page, expr):
    return page.evaluate(f"() => {expr}")


# ---------- routing ----------

def test_pen_paints(paint):
    page = paint()
    x, y = blank_run(page)
    before = steps(page)
    row_stroke(page, x, y, 4)
    colour = state_of(page, "state.color")
    assert [pixel_at(page, x + i, y) for i in range(4)] == [colour] * 4
    assert steps(page) == before + 1


def test_pen_paints_at_the_default_fit(open_view):
    """No setZoom: the first stroke of a visit lands on the pixel under the pen at the zoom the page chose (UX M2-A: the header
    no longer reflows mid-stroke, so no test needs to reserve room for the Unsaved pill)."""
    page = open_view(VIEW, LAND)
    page.locator("#stage .paint-canvas canvas").scroll_into_view_if_needed()
    x, y = blank_run(page)
    before = steps(page)
    row_stroke(page, x, y, 4)
    colour = state_of(page, "state.color")
    assert [pixel_at(page, x + i, y) for i in range(4)] == [colour] * 4
    assert steps(page) == before + 1


def test_finger_paints_only_while_pencil_only_is_off(paint):
    page = paint()
    x, y = blank_run(page)
    start, end = screen(page, x, y), screen(page, x + 3, y)
    touch_start(page, [start])
    drag(page, [start], [end])
    touch_end(page)
    colour = state_of(page, "state.color")
    assert [pixel_at(page, x + i, y) for i in range(4)] == [colour] * 4

    page = paint(pencil_only=True)
    before, history = data(page), steps(page)
    touch_start(page, [start])
    drag(page, [start], [end])
    touch_end(page)
    assert data(page) == before and steps(page) == history


def test_pencil_only_tap_shows_that_pixel(paint):
    page = paint(pencil_only=True)
    x, y = blank_run(page)
    tap(page, [screen(page, x, y)])
    assert f"x {x}, y {y}" in page.locator("#paint-readout").inner_text()
    assert steps(page) == 1


def test_pencil_only_switch_on_the_quick_bar(paint):
    page = paint()
    button = page.locator("#qb-pencil")
    assert button.get_attribute("aria-pressed") == "false"
    button.tap()
    assert button.get_attribute("aria-pressed") == "true" and page.evaluate("() => JelliTouch.pencilOnly()")
    assert "Pencil only is on" in page.locator("#paint-hint").inner_text()
    button.tap()
    assert not page.evaluate("() => JelliTouch.pencilOnly()")


def test_second_finger_cancels_a_fresh_stroke_with_clean_history(paint):
    page = paint()
    x, y = blank_run(page)
    before, history = data(page), steps(page)
    a, b = screen(page, x, y), screen(page, x + 8, y)
    touch_start(page, [a])
    touch_start(page, [a, b])
    touch_end(page)
    assert data(page) == before and steps(page) == history
    assert page.locator("#paint-history .redo").count() == 0

    # a second finger after the window is ignored and the stroke stays
    touch_start(page, [a])
    page.wait_for_timeout(thresholds(page)["SECOND_FINGER_MS"] + 150)
    touch_start(page, [a, b])
    touch_end(page)
    assert data(page) != before and steps(page) == history + 1


def test_second_finger_after_a_fill_leaves_nothing(paint):
    page = paint()
    page.locator("#tools [data-tool=fill]").tap()
    x, y = blank_run(page)
    before, history = data(page), steps(page)
    a, b = screen(page, x, y), screen(page, x + 8, y)
    touch_start(page, [a])
    touch_start(page, [a, b])
    touch_end(page)
    assert data(page) == before and steps(page) == history
    assert page.locator("#paint-history .redo").count() == 0


def test_palm_near_the_pen_never_paints(paint):
    page = paint()
    x, y = blank_run(page, 6)
    px, py = blank_run(page, 6, skip=8)
    pen_down(page, *screen(page, x, y))
    touch_start(page, [screen(page, px, py)])
    pen_move(page, *screen(page, x + 3, y))
    pen_up(page, *screen(page, x + 3, y))
    touch_end(page)
    assert pixel_at(page, px, py) is None
    tap(page, [screen(page, px, py)])  # just after the pen lifted
    assert pixel_at(page, px, py) is None
    page.wait_for_timeout(thresholds(page)["PALM_MS"] + 100)
    tap(page, [screen(page, px, py)])
    assert pixel_at(page, px, py) is not None


# ---------- gestures ----------

def test_pinch_commits_one_whole_zoom_at_the_pinch_point(paint):
    page = paint(pencil_only=True)
    page.evaluate("() => setZoom(20)")  # big enough to scroll, so the pinch point can stay put
    page.wait_for_timeout(100)
    box = page.locator("#stage .paint-canvas .scroller").bounding_box()
    mid = (box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)
    under = lambda: page.evaluate("""([x, y]) => { const c = document.querySelector('#stage .paint-canvas canvas').getBoundingClientRect(), a = byKey[state.key];
        const z = c.width / a.width; return [(x - c.left) / z, (y - c.top) / z]; }""", list(mid))
    pixel_before = under()
    pinch(page, mid, 100, 120, lift=False)
    assert "scale" in page.evaluate("() => document.querySelector('#stage .paint-canvas canvas').style.transform")
    assert page.evaluate("() => state.zoom") == 20  # previewed only; nothing committed yet
    touch_end(page)
    assert page.evaluate("() => state.zoom") == 24  # one whole-number zoom, committed once
    assert page.evaluate("() => document.querySelector('#stage .paint-canvas canvas').style.transform") == ""
    after = under()
    assert abs(after[0] - pixel_before[0]) <= 1 and abs(after[1] - pixel_before[1]) <= 1
    assert steps(page) == 1


def test_two_finger_drag_pans_without_painting(paint):
    page = paint()
    page.evaluate("() => setZoom(32)")
    page.wait_for_timeout(100)
    scroller = "#stage .paint-canvas .scroller"
    before = page.evaluate(f"() => document.querySelector('{scroller}').scrollLeft")
    box = page.locator(scroller).bounding_box()
    pan(page, (box["x"] + box["width"] / 2, box["y"] + box["height"] / 2), (-60, 0))
    after = page.evaluate(f"() => document.querySelector('{scroller}').scrollLeft")
    assert 50 <= after - before <= 70
    assert steps(page) == 1


def test_two_finger_tap_undoes_and_three_finger_tap_redoes(paint):
    page = paint()
    x, y = blank_run(page)
    row_stroke(page, x, y, 3)
    painted = data(page)
    assert current_step(page) == 1
    tap(page, [screen(page, x + 6, y + 6), screen(page, x + 8, y + 6)])
    assert current_step(page) == 0 and data(page) != painted and pixel_at(page, x, y) is None
    tap(page, [screen(page, x + 6, y + 6), screen(page, x + 8, y + 6), screen(page, x + 10, y + 6)])
    assert current_step(page) == 1 and data(page) == painted


def test_finger_long_press_picks_and_keeps_the_tool(paint):
    for pencil_only in (False, True):
        page = paint(pencil_only=pencil_only)
        px, py, hex_ = page.evaluate("""() => { const p = decoded[state.key].after;
            for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) { const h = pixel(p, x, y); if (h && h !== state.color && (byKey[state.key].palette || D.palette).map(h => h.toLowerCase()).includes(h)) return [x, y, h]; } }""")
        page.locator("#tools [data-tool=line]").tap()
        before = data(page)
        long_press(page, screen(page, px, py))
        assert page.evaluate("() => state.color") == hex_
        assert page.evaluate("() => state.tool") == "line"
        assert data(page) == before and steps(page) == 1


def test_pen_hold_does_not_pick(paint):
    page = paint()
    x, y = blank_run(page)
    colour = state_of(page, "state.color")
    pen_stroke(page, [screen(page, x, y), screen(page, x + 1, y)], hold_ms=thresholds(page)["LONG_PRESS_MS"] + 200)
    assert pixel_at(page, x, y) == colour and steps(page) == 2


def test_first_pen_offers_pencil_only_once(paint):
    page = paint()
    x, y = blank_run(page)
    row_stroke(page, x, y, 2)
    offer = page.get_by_role("button", name="Turn on Pencil only")
    offer.tap()
    assert page.evaluate("() => JelliTouch.pencilOnly()")


# ---------- the quick bar ----------

def test_quick_bar_controls_are_touch_sized(paint):
    for size in (LAND, PORT):
        page = paint(size)
        page.locator("#tools [data-tool=select]").tap()
        x, y = blank_run(page)
        pen_stroke(page, [screen(page, x, y), screen(page, x + 3, y + 3)])  # a selection shows the selection row
        assert page.locator("#qb-sel").is_visible()
        assert small_targets(page, "#paint-qbar") == [], size
        assert small_targets(page, "#tools") == [], size


def test_quick_bar_layout_follows_orientation_and_handedness(paint):
    page = paint(LAND)
    bar, cv, side = (page.locator(s).bounding_box() for s in ("#paint-qbar", "#stage .paint-canvas canvas", "#stage .paint-side"))
    assert bar["x"] + bar["width"] <= cv["x"] + 1
    page.evaluate("() => document.getElementById('lefty').click()")
    bar, side = page.locator("#paint-qbar").bounding_box(), page.locator("#stage .paint-side").bounding_box()
    assert bar["x"] >= side["x"] + side["width"] - 1
    page = paint(PORT)
    bar, cv = page.locator("#paint-qbar").bounding_box(), page.locator("#stage .paint-canvas canvas").bounding_box()
    assert bar["y"] >= cv["y"] + cv["height"] - 1


def test_quick_bar_is_hidden_with_a_mouse_unless_asked(open_view):
    page = open_view(VIEW, "desktop")
    bar = page.locator("#paint-qbar")
    assert not bar.is_visible()
    assert "Right-click paints the secondary colour" in page.locator("#paint-hint").inner_text()
    page.locator("#paint-more summary").click()
    page.locator("#quickbar").click()
    assert bar.is_visible() and page.locator("#quickbar").get_attribute("aria-pressed") == "true"
    page.reload()
    page.wait_for_function("() => typeof D !== 'undefined' && D.assets && D.assets.length > 0")
    page.locator("#paint-qbar").wait_for(state="visible")


def test_second_colour_modifier_held_and_latched(paint):
    page = paint()
    x, y = blank_run(page)
    row_stroke(page, x, y, 4)  # painted in the main colour
    page.evaluate("() => { state.color2 = null; }")
    # held by a thumb while the Pencil draws
    touch_start(page, [centre(page, "#qb-second")])
    pen_stroke(page, [screen(page, x, y), screen(page, x + 1, y)])
    touch_end(page)
    assert [pixel_at(page, x + i, y) is None for i in range(4)] == [True, True, False, False]
    assert page.locator("#qb-second").get_attribute("aria-pressed") == "false"
    # latched by a tap, until tapped again
    page.locator("#qb-second").tap()
    assert page.locator("#qb-second").get_attribute("aria-pressed") == "true"
    pen_stroke(page, [screen(page, x + 2, y), screen(page, x + 3, y)])
    assert [pixel_at(page, x + i, y) is None for i in range(4)] == [True] * 4
    page.locator("#qb-second").tap()
    row_stroke(page, x, y, 2)
    assert pixel_at(page, x, y) == state_of(page, "state.color")


def test_pick_modifier_picks_and_becomes_copy_with_select(paint):
    page = paint()
    x, y = blank_run(page)
    other = page.evaluate("() => (byKey[state.key].palette || D.palette).map(h => h.toLowerCase()).find(h => h !== state.color)")
    page.evaluate("c => { state.color = c; }", other)
    row_stroke(page, x, y, 2)
    page.evaluate("() => { state.color = '#291b35'; }")
    page.locator("#qb-pick").tap()
    before = data(page)
    pen_stroke(page, [screen(page, x, y), screen(page, x, y)])
    assert page.evaluate("() => state.color") == other and data(page) == before and page.evaluate("() => state.tool") == "pencil"
    page.locator("#tools [data-tool=select]").tap()
    assert page.locator("#qb-pick").inner_text() == "Copy"
    page.locator("#tools [data-tool=pencil]").tap()
    assert page.locator("#qb-pick").inner_text() == "Pick"


def test_select_copy_modifier_copies_the_selection(paint):
    page = paint()
    x, y = blank_run(page, 8)
    row_stroke(page, x, y, 2)
    page.locator("#tools [data-tool=select]").tap()
    pen_stroke(page, [screen(page, x, y), screen(page, x + 1, y)])
    assert page.evaluate("() => !!state.sel")
    page.locator("#qb-pick").tap()  # Copy
    pen_stroke(page, [screen(page, x, y), screen(page, x + 4, y)])
    assert [pixel_at(page, x + i, y) is not None for i in (0, 1, 4, 5)] == [True, True, True, True]


def test_constrain_modifier_matches_shift(open_view, paint):
    mouse = ready(open_view(VIEW, LAND))
    x, y = blank_run(mouse)
    mouse.locator("#tools [data-tool=line]").click()
    start, end = (x, y), (x + 9, y + 5)
    mouse.keyboard.down("Shift")
    mouse.mouse.move(*screen(mouse, *start))
    mouse.mouse.down()
    mouse.mouse.move(*screen(mouse, *end), steps=6)
    mouse.mouse.up()
    mouse.keyboard.up("Shift")
    expected = data(mouse)

    page = paint()
    page.locator("#tools [data-tool=line]").tap()
    assert page.locator("#qb-constrain").inner_text() == "Snap"
    touch_start(page, [centre(page, "#qb-constrain")])  # held
    pen_stroke(page, [screen(page, *start), screen(page, *end)])
    touch_end(page)
    assert data(page) == expected
    # and without it the line is the plain one
    page.locator("#qb-undo").tap()
    pen_stroke(page, [screen(page, *start), screen(page, *end)])
    assert data(page) != expected
    page.locator("#tools [data-tool=rect]").tap()
    assert page.locator("#qb-constrain").inner_text() == "Square"
    page.locator("#tools [data-tool=pencil]").tap()
    assert page.locator("#qb-constrain").is_disabled()


def test_peek_is_momentary_when_held_and_latches_on_a_tap(paint):
    page = paint()
    button = "#qb-peek"
    touch_start(page, [centre(page, button)])
    page.wait_for_timeout(thresholds(page)["TAP_UP_MS"] + 150)
    assert page.evaluate("() => state.peek") is True and page.evaluate("() => document.body.classList.contains('peeking')")
    touch_end(page)
    assert page.evaluate("() => state.peek") is False
    page.locator(button).tap()
    assert page.evaluate("() => state.peek") is True and page.locator(button).get_attribute("aria-pressed") == "true"
    page.locator(button).tap()
    assert page.evaluate("() => state.peek") is False and page.locator(button).get_attribute("aria-pressed") == "false"


def test_swap_undo_and_redo_buttons(paint):
    page = paint()
    page.evaluate("() => { state.color2 = '#ffffff'; state.custom2 = true; }")
    first = page.evaluate("() => state.color")
    page.locator("#qb-swap").tap()
    assert page.evaluate("() => [state.color, state.color2]") == ["#ffffff", first]
    x, y = blank_run(page)
    assert page.locator("#qb-undo").is_disabled() and page.locator("#qb-redo").is_disabled()
    row_stroke(page, x, y, 3)
    assert current_step(page) == 1
    page.locator("#qb-undo").tap()
    assert current_step(page) == 0 and page.locator("#qb-redo").is_enabled()
    page.locator("#qb-redo").tap()
    assert current_step(page) == 1


def test_selection_row_nudges_and_deselects(paint):
    page = paint()
    sel_row = page.locator("#qb-sel")
    assert not sel_row.is_visible()
    x, y = blank_run(page, 8)
    row_stroke(page, x, y, 2)
    page.locator("#tools [data-tool=select]").tap()
    pen_stroke(page, [screen(page, x, y), screen(page, x + 1, y)])
    assert sel_row.is_visible()
    page.evaluate("() => { window.__sx = state.sel.x; }")
    page.locator("#qb-nudge-right").tap()
    assert page.evaluate("() => state.sel.x - window.__sx") == 1
    assert pixel_at(page, x, y) is None and pixel_at(page, x + 2, y) is not None
    page.locator("#qb-deselect").tap()
    assert page.evaluate("() => state.sel") is None and not sel_row.is_visible()


# ---------- the colour menu and help ----------

@pytest.mark.parametrize("view", ["paint-creature", "paint-icon"])
def test_long_press_on_a_colour_opens_the_menu(open_view, view):
    page = open_view(view, LAND)
    hex_ = page.evaluate("() => (byKey[state.key].palette || D.palette).map(h => h.toLowerCase()).find(h => h !== state.color)")
    chip = page.locator(f'.paint-chip[data-chip$="{hex_}"]').first
    chip.scroll_into_view_if_needed()
    long_press(page, centre(page, f'.paint-chip[data-chip$="{hex_}"]'))
    menu = page.locator(".paint-menu")
    assert menu.is_visible()
    assert menu.get_by_role("menuitem").all_inner_texts()[:2] == ["Use as secondary", "Isolate"]
    editable = page.locator(".paint-slot").count() > 0  # only the shared palette has slots to edit
    assert ("Edit slot" in menu.inner_text()) == editable
    assert small_targets(page, ".paint-menu") == []
    assert page.evaluate("() => state.color") != hex_  # the click that ends the press is swallowed
    menu.get_by_role("menuitem", name="Isolate").tap()
    assert page.evaluate("() => state.isolate") == hex_ and not menu.is_visible()
    long_press(page, centre(page, f'.paint-chip[data-chip$="{hex_}"]'))
    page.get_by_role("menuitem", name="Use as secondary").tap()
    assert page.evaluate("() => state.color2") == hex_


def test_help_lists_the_paint_gestures_and_the_hint_uses_touch_wording(paint):
    page = paint(PORT)
    assert "Two-finger tap undoes" in page.locator("#paint-hint").inner_text()
    page.evaluate("() => JelliShell.openHelp()")
    section = page.locator("dialog[open] section", has=page.get_by_role("heading", name="Touch and Pencil"))
    text = section.inner_text()
    assert "Pinch" in text and "Two-finger tap" in text and "Three-finger tap" in text and "Quick bar: hold" in text


def test_mouse_and_keyboard_still_work(open_view):
    page = open_view(VIEW, "desktop")
    x, y = blank_run(page)
    page.evaluate("() => { state.color2 = null; }")
    page.mouse.move(*screen(page, x, y))
    page.mouse.down()
    page.mouse.move(*screen(page, x + 2, y), steps=4)
    page.mouse.up()
    colour = state_of(page, "state.color")
    assert pixel_at(page, x, y) == colour
    page.mouse.move(*screen(page, x, y))
    page.mouse.down(button="right")
    page.mouse.up(button="right")
    assert pixel_at(page, x, y) is None  # right-click paints the transparent secondary
    page.keyboard.press("x")
    assert page.evaluate("() => state.color") is None
    page.keyboard.down("Alt")
    page.mouse.click(*screen(page, x + 1, y))
    page.keyboard.up("Alt")
    assert page.evaluate("() => state.color") == colour
