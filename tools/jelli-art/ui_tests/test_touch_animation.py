"""RFC-007 T2: the Creature timeline and the flip-book with fingers and a Pencil, driven with emulated touch
(touch.py) at both iPad sizes: duration handles (coarse step, Fine 1 ms), scrubbing and tapping frames, the Move
buttons, play and onion controls by tap, 44 px targets, and the Creature layout not scrolling sideways.

Run: tools/jelli-art/ui_tests/run.sh -k touch_animation
"""
import pytest

import touch
from conftest import TOUCH_VIEWPORTS, new_view
from touch import drag, pen_stroke, sideways_overflow, small_targets, tap, touch_end, touch_move, touch_start

SIZES = list(TOUCH_VIEWPORTS)
CLIP = "() => { const c = Studio.cr.current(Studio.cr.selectedKey()); return {frames: [...c.frames], ms: [...c.durations_ms]}; }"


@pytest.fixture(params=SIZES)
def creature(request, browser, studio):
    """The Creature view on the axolotl idle clip (four frames), at an iPad size."""
    context, page = new_view(browser, studio, "creature", request.param)
    page.evaluate("""() => { state.cForm = 'axolotl'; state.cPose = 'idle'; Studio.cr.rerender(); }""")  # idle has four frames
    page.wait_for_selector("#an-track .an-handle")
    page.evaluate("() => { Studio.cr.setPlaying(false); Studio.cr.seek(0); }")
    yield page
    touch_end(page)
    context.close()


def center(page, selector):
    """Viewport centre of the first element matching selector, after scrolling it into view."""
    return page.evaluate("""s => { const el = document.querySelector(s); el.scrollIntoView({block: 'center', inline: 'nearest'});
        const r = el.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; }""", selector)


def tap_on(page, selector):
    tap(page, [tuple(center(page, selector))])
    page.wait_for_timeout(80)


def durations(page):
    return page.evaluate(CLIP)["ms"]


def per_ms(page):
    return page.evaluate("() => { const t = document.getElementById('an-track'); return t.clientWidth / Studio.cr.totalOf(Studio.cr.current(Studio.cr.selectedKey())); }")


def drag_handle(page, index, dx, finger=True):
    x, y = center(page, f'.an-handle[data-index="{index}"]')
    if finger:
        touch_start(page, [(x, y)])
        drag(page, [(x, y)], [(x + dx, y)])
        touch_end(page)
    else:
        pen_stroke(page, [(x, y), (x + dx, y)])
    page.wait_for_timeout(120)


def test_handle_hit_area_is_44px(creature):
    center(creature, ".an-handle")
    handles = creature.evaluate("""() => [...document.querySelectorAll('.an-handle')].map(h => {
        const r = h.getBoundingClientRect(); return [r.width, r.height]; })""")
    assert handles and all(w >= 44 and h >= 44 for w, h in handles)
    # and the whole box answers to the handle, including the half that overlaps the next segment
    hit = creature.evaluate("""() => { const h = document.querySelector('.an-handle[data-index="0"]'), r = h.getBoundingClientRect();
        return [-20, 20].map(d => document.elementFromPoint(r.left + r.width / 2 + d, r.top + r.height / 2)?.closest('.an-handle')?.dataset.index); }""")
    assert hit == ["0", "0"]


@pytest.mark.parametrize("finger", [True, False], ids=["finger", "pencil"])
def test_handle_drag_changes_duration_in_coarse_steps(creature, finger):
    before = durations(creature)
    dx = 33 * per_ms(creature) + 0.4  # about +33 ms
    drag_handle(creature, 1, dx, finger)
    after = durations(creature)
    assert after[1] != before[1] and after[1] % 10 == 0
    assert abs(after[1] - (before[1] + 33)) <= 10
    assert after[:1] + after[2:] == before[:1] + before[2:]


def test_fine_toggle_moves_one_millisecond(creature):
    assert creature.locator("#an-fine").get_attribute("aria-pressed") == "false"
    tap_on(creature, "#an-fine")
    assert creature.locator("#an-fine").get_attribute("aria-pressed") == "true"
    before = durations(creature)
    drag_handle(creature, 0, 3 * per_ms(creature) + 0.2)  # a coarse drag could not land on +3
    after = durations(creature)
    assert 2 <= after[0] - before[0] <= 4  # 1 ms steps; a coarse drag could only land on 0 or 10
    tap_on(creature, "#an-fine")
    assert creature.locator("#an-fine").get_attribute("aria-pressed") == "false"


def test_fine_toggle_sets_the_keyboard_step(creature):
    handle = creature.locator('.an-handle[data-index="0"]')
    start = durations(creature)[0]
    handle.focus()
    creature.keyboard.press("ArrowRight")
    assert durations(creature)[0] == start + 10
    tap_on(creature, "#an-fine")
    creature.locator('.an-handle[data-index="0"]').focus()
    creature.keyboard.press("ArrowRight")
    assert durations(creature)[0] == start + 11
    creature.keyboard.press("Shift+ArrowRight")  # Shift still works for keyboard users
    assert durations(creature)[0] == start + 111


def seg_rects(page):
    center(page, "#an-track")
    return page.evaluate("""() => [...document.querySelectorAll('.an-seg')].map(s => { const r = s.getBoundingClientRect(); return [r.left, r.top, r.width, r.height]; })""")


def test_dragging_along_the_track_scrubs_frames(creature):
    rects = seg_rects(creature)
    y = rects[0][1] + rects[0][3] / 2
    start, end = (rects[0][0] + 12, y), (rects[2][0] + 40, y)
    touch_start(creature, [start])
    assert creature.evaluate("() => Studio.cr.pausedFrame()") == 0
    drag(creature, [start], [end])
    touch_end(creature)
    creature.wait_for_timeout(100)
    assert creature.evaluate("() => Studio.cr.pausedFrame()") == 2
    assert creature.evaluate("() => document.querySelector('.an-seg.now')?.dataset.index") == "2"
    assert durations(creature) == creature.evaluate(CLIP)["ms"]  # scrubbing never edits durations


def test_scrubbing_back_with_the_pencil(creature):
    rects = seg_rects(creature)
    y = rects[0][1] + rects[0][3] / 2
    pen_stroke(creature, [(rects[2][0] + 40, y), (rects[0][0] + 12, y)])
    creature.wait_for_timeout(100)
    assert creature.evaluate("() => Studio.cr.pausedFrame()") == 0


def test_tap_selects_a_frame(creature):
    tap_on(creature, '.an-card[data-index="2"] .thumb-btn')
    assert creature.evaluate("() => Studio.cr.pausedFrame()") == 2
    left, top, _, height = seg_rects(creature)[1]
    tap(creature, [(left + 40, top + height / 2)])
    creature.wait_for_timeout(80)
    assert creature.evaluate("() => Studio.cr.pausedFrame()") == 1


def test_move_buttons_reorder_frames(creature):
    before = creature.evaluate(CLIP)
    tap_on(creature, '.an-card[data-index="1"] [data-act="right"]')
    after = creature.evaluate(CLIP)
    assert after["frames"][1] == before["frames"][2] and after["frames"][2] == before["frames"][1]
    assert after["ms"][1] == before["ms"][2]  # the duration travels with its frame
    tap_on(creature, '.an-card[data-index="2"] [data-act="left"]')
    assert creature.evaluate(CLIP) == before


def test_creature_transport_by_tap(creature):
    tap_on(creature, "#cr-play")
    assert creature.evaluate("() => Studio.cr.playing()") is True
    tap_on(creature, "#cr-play")
    assert creature.evaluate("() => Studio.cr.playing()") is False
    tap_on(creature, "#cr-next")
    assert creature.evaluate("() => Studio.cr.pausedFrame()") == 1


def test_touch_targets_are_44px_in_the_creature_view(creature):
    for root in ("#cr-forms", "#cr-transport", "#cr-editor", "#cr-strip"):
        assert small_targets(creature, root) == [], root


def test_info_buttons_replace_title_only_help(creature):
    assert creature.locator("#an-dur-label ~ .jt-info, .an-dur .jt-info").count() == 1
    tap_on(creature, ".an-dur .jt-info")
    assert "Fine" in creature.locator("#jt-pop").inner_text()
    assert creature.locator("#cr-transport .jt-info").count() == 1


def test_creature_does_not_scroll_sideways(creature):
    creature.wait_for_timeout(200)
    assert sideways_overflow(creature) == 0


def test_creature_layout_follows_the_container(creature):
    columns = creature.evaluate("() => getComputedStyle(document.querySelector('.cr-layout')).gridTemplateColumns.split(' ').length")
    wide = creature.evaluate("() => document.getElementById('creature').clientWidth >= 900")
    assert columns == (2 if wide else 1)


# ---- the flip-book, beside the Paint canvas ----

@pytest.fixture(params=SIZES)
def flipbook(request, browser, studio):
    context, page = new_view(browser, studio, "paint-creature", request.param)
    page.wait_for_selector("#fb-play")
    yield page
    touch_end(page)
    context.close()


def test_flipbook_controls_by_tap(flipbook):
    assert flipbook.locator("#fb-play").get_attribute("aria-pressed") == "false"
    tap_on(flipbook, "#fb-play")
    assert flipbook.locator("#fb-play").get_attribute("aria-pressed") == "true"
    tap_on(flipbook, "#fb-play")
    assert flipbook.locator("#fb-play").get_attribute("aria-pressed") == "false"

    on = flipbook.evaluate("() => state.onionSkin")
    tap_on(flipbook, "#fb-onion")
    assert flipbook.evaluate("() => state.onionSkin") is (not on)
    tap_on(flipbook, "#fb-onion")
    assert flipbook.evaluate("() => state.onionSkin") is on

    mode = flipbook.evaluate("() => state.onionMode")
    other = "full" if mode == "diff" else "diff"  # Shift+O on screen
    tap_on(flipbook, f'[data-onion-mode="{other}"]')
    assert flipbook.evaluate("() => state.onionMode") == other


def test_flipbook_frame_buttons_change_the_painted_frame(flipbook):
    key = flipbook.evaluate("() => asset().key")
    tap_on(flipbook, "#fb-next")
    flipbook.wait_for_timeout(150)
    assert flipbook.evaluate("() => asset().key") != key
    tap_on(flipbook, "#fb-prev")
    flipbook.wait_for_timeout(150)
    assert flipbook.evaluate("() => asset().key") == key


def test_flipbook_touch_targets_are_44px(flipbook):
    assert small_targets(flipbook, ".fb") == []
    flipbook.wait_for_timeout(100)
    assert sideways_overflow(flipbook) == 0


def test_gestures_are_listed_in_help(creature):
    creature.keyboard.press("?")
    creature.wait_for_timeout(150)
    text = creature.evaluate("() => document.body.innerText")
    assert "Drag a duration handle" in text and "Drag along the timeline track" in text
