"""RFC-007 T0: the touch.js gesture recogniser at its thresholds, the Pencil only switch, and the shell's touch
furniture (sizes, keyboard hints, the long-press popover, ⓘ help, the List drawer, toasts, the help section),
driven with emulated Pencil and finger input (touch.py) at iPad sizes.

Run: tools/jelli-art/ui_tests/run.sh -k touch_foundation
"""
import pytest

from conftest import new_view
from touch import (drag, long_press, pan, pen_down, pen_move, pen_stroke, pen_up, pinch, small_targets, tap,
                   thresholds, touch_end, touch_move, touch_start)

VIEW = "review-detail"

# A 600 x 600 surface pinned over the page with a recogniser that logs every callback to window.__log.
SURFACE = """pencilOnly => {
    window.__detach?.(); document.getElementById('jt-test')?.remove();
    const el = document.createElement('div'); el.id = 'jt-test';
    Object.assign(el.style, {position: 'fixed', left: '0', top: '0', width: '600px', height: '600px', zIndex: '1000', background: '#444'});
    document.body.append(el);
    window.__log = []; window.__po = pencilOnly;
    const log = (...a) => window.__log.push(a);
    const g = JelliTouch.gestures({
      stroke: {start: e => log('start', e.pointerType), move: e => log('move', e.pointerType), end: e => log('end', e.pointerType),
        cancel: e => log('cancel', e.pointerType)},
      pan: (dx, dy) => log('pan', dx, dy), pinch: g => log('pinch', g.scale), gestureEnd: g => log('gestureEnd', g.scale),
      tap: n => log('tap', n), point: () => log('point'), longPress: e => log('longPress', e.pointerType)},
      {pencilOnly: () => window.__po});
    window.__detach = g.attach(el);
  }"""


@pytest.fixture(scope="module")
def ipad(browser, studio):
    context, page = new_view(browser, studio, VIEW, "ipad-portrait")
    yield page
    context.close()


@pytest.fixture
def surface(ipad):
    """surface(pencil_only=False) installs a fresh logging recogniser and returns the page."""
    def install(pencil_only=False):
        ipad.evaluate(SURFACE, pencil_only)
        return ipad
    yield install
    touch_end(ipad)


def log(page):
    page.wait_for_timeout(30)  # let the last events dispatch
    return [tuple(entry) for entry in page.evaluate("() => window.__log")]


def names(page):
    return [entry[0] for entry in log(page)]


def test_coarse_media_and_thresholds(ipad):
    assert ipad.evaluate("() => matchMedia('(any-pointer: coarse)').matches && JelliTouch.coarse()")
    t = thresholds(ipad)
    assert t == {"SECOND_FINGER_MS": 250, "PALM_MS": 300, "PAN_PX": 8, "PINCH_RATIO": 0.12, "TAP_DOWN_MS": 100,
                 "TAP_UP_MS": 350, "TAP_MOVE_PX": 12, "LONG_PRESS_MS": 500, "LONG_PRESS_MOVE_PX": 8}


def test_pen_strokes_and_never_long_presses(surface):
    page = surface()
    pen_stroke(page, [(100, 100), (160, 120)], hold_ms=thresholds(page)["LONG_PRESS_MS"] + 200)
    got = log(page)
    assert got[0] == ("start", "pen") and got[-1] == ("end", "pen")
    assert {e[0] for e in got} == {"start", "move", "end"}


def test_finger_paints_only_while_pencil_only_is_off(surface):
    page = surface(pencil_only=False)
    touch_start(page, [(100, 100)])
    drag(page, [(100, 100)], [(160, 100)])
    touch_end(page)
    got = names(page)
    assert got[0] == "start" and got[-1] == "end" and "pan" not in got

    page = surface(pencil_only=True)
    touch_start(page, [(100, 100)])
    drag(page, [(100, 100)], [(160, 100)])
    touch_end(page)
    got = log(page)
    assert "start" not in [e[0] for e in got]
    assert sum(e[1] for e in got if e[0] == "pan") == pytest.approx(60, abs=1)
    assert got[-1][0] == "gestureEnd"

    page = surface(pencil_only=True)
    tap(page, [(100, 100)])
    assert names(page) == ["point"]


def test_second_finger_cancels_only_a_fresh_stroke(surface):
    page = surface()
    touch_start(page, [(100, 100)])
    touch_start(page, [(100, 100), (300, 100)])
    touch_end(page)
    got = names(page)
    assert got[:2] == ["start", "cancel"] and "end" not in got

    page = surface()
    touch_start(page, [(100, 100)])
    page.wait_for_timeout(thresholds(page)["SECOND_FINGER_MS"] + 150)
    touch_start(page, [(100, 100), (300, 100)])
    drag(page, [(100, 100), (300, 100)], [(140, 100), (340, 100)])
    touch_end(page)
    got = names(page)
    assert got[0] == "start" and got[-1] == "end"
    assert not {"cancel", "tap", "pan", "pinch"} & set(got)


def test_palm_never_paints_near_the_pen(surface):
    page = surface()
    pen_down(page, 100, 100)
    touch_start(page, [(400, 400)])  # palm while the pen is down
    pen_move(page, 120, 100)
    pen_up(page, 120, 100)
    touch_end(page)
    assert [e for e in log(page) if e[1:] == ("touch",)] == []

    page = surface()
    pen_stroke(page, [(100, 100), (120, 100)])
    tap(page, [(400, 400)])  # just after the pen lifts
    assert [e for e in log(page) if e[1:] == ("touch",)] == []
    page.wait_for_timeout(thresholds(page)["PALM_MS"] + 100)
    tap(page, [(400, 400)])
    assert ("start", "touch") in log(page)


def test_two_and_three_finger_taps(surface):
    page = surface()
    tap(page, [(200, 300), (300, 300)])
    got = names(page)
    assert got[-1] == "tap" and log(page)[-1] == ("tap", 2) and "end" not in got  # the first finger's stroke is cancelled

    page = surface(pencil_only=True)
    tap(page, [(200, 300), (300, 300), (400, 300)])
    assert names(page) == ["tap"] and log(page)[-1] == ("tap", 3)

    t = thresholds(page)
    page = surface(pencil_only=True)
    tap(page, [(200, 300), (300, 300)], hold_ms=t["TAP_UP_MS"] + 150)
    assert "tap" not in names(page)

    page = surface(pencil_only=True)  # a 10 px wobble of one finger is still a tap
    touch_start(page, [(200, 300), (300, 300)])
    touch_move(page, [(200, 300), (310, 300)])
    touch_end(page)
    assert names(page) == ["tap"]

    page = surface(pencil_only=True)  # past TAP_MOVE_PX it is not
    touch_start(page, [(200, 300), (300, 300)])
    touch_move(page, [(200, 300), (300, 300 + t["TAP_MOVE_PX"] + 8)])
    touch_end(page)
    assert "tap" not in names(page)


def test_pinch_and_pan_thresholds(surface):
    page = surface(pencil_only=True)
    pinch(page, (300, 300), 200, 300)
    got = log(page)
    pinches = [e[1] for e in got if e[0] == "pinch"]
    assert pinches and pinches[-1] == pytest.approx(1.5, abs=0.02)
    assert got[-1][0] == "gestureEnd" and got[-1][1] == pytest.approx(1.5, abs=0.02)
    assert "pan" not in [e[0] for e in got]

    page = surface(pencil_only=True)  # 10% is under PINCH_RATIO
    pinch(page, (300, 300), 200, 220)
    assert "pinch" not in names(page)

    page = surface(pencil_only=True)
    pan(page, (300, 300), (40, -20))
    got = log(page)
    assert sum(e[1] for e in got if e[0] == "pan") == pytest.approx(40, abs=1)
    assert sum(e[2] for e in got if e[0] == "pan") == pytest.approx(-20, abs=1)
    assert "pinch" not in [e[0] for e in got] and got[-1][0] == "gestureEnd"

    page = surface(pencil_only=True)  # 6 px is under PAN_PX
    pan(page, (300, 300), (6, 0))
    assert "pan" not in names(page)


def test_long_press_picks_with_a_finger_only(surface):
    page = surface(pencil_only=False)
    long_press(page, (200, 200))
    got = names(page)
    assert got[:2] == ["start", "cancel"] and "longPress" in got and "end" not in got

    page = surface(pencil_only=True)
    long_press(page, (200, 200))
    assert names(page) == ["longPress"]

    t = thresholds(page)
    page = surface(pencil_only=True)  # moving past LONG_PRESS_MOVE_PX first: no pick
    touch_start(page, [(200, 200)])
    touch_move(page, [(200 + t["LONG_PRESS_MOVE_PX"] + 4, 200)])
    page.wait_for_timeout(t["LONG_PRESS_MS"] + 150)
    touch_end(page)
    assert "longPress" not in names(page)


def test_pen_ends_a_finger_gesture(surface):
    page = surface()
    pan(page, (300, 300), (40, 0), lift=False)
    pen_down(page, 100, 500)
    got = names(page)
    assert got[-2:] == ["gestureEnd", "start"] and log(page)[-1] == ("start", "pen")
    touch_move(page, [(200, 300), (400, 300)])  # the fingers stay inert
    pen_up(page, 100, 500)
    touch_end(page)
    assert names(page)[-1] == "end"


# ---------- page furniture ----------

def test_shell_controls_are_touch_sized(browser, studio):
    for size in ("ipad-portrait", "ipad-landscape"):
        context, page = new_view(browser, studio, VIEW, size)
        try:
            assert small_targets(page, "#shell-modes") == []
            assert small_targets(page, ".shell-tools") == []
        finally:
            context.close()


def test_list_drawer(open_view):
    page = open_view(VIEW, "ipad-portrait")
    aside, button = page.locator("aside"), page.locator("#jt-list-button")
    assert button.is_visible() and not aside.is_visible()
    button.tap()
    assert aside.is_visible() and button.get_attribute("aria-expanded") == "true"
    page.locator(".jt-scrim").tap(position={"x": 700, "y": 600})
    assert not aside.is_visible() and button.get_attribute("aria-expanded") == "false"
    button.tap()
    item = page.locator("#list .item").nth(2)
    key = item.get_attribute("data-key")
    item.tap()
    assert not aside.is_visible()
    if key:
        assert page.evaluate("() => state.key") == key


def test_keyboard_hints_wait_for_a_keyboard(open_view):
    page = open_view(VIEW, "ipad-portrait")
    hint = page.locator("#shell-help-button kbd")
    assert not hint.is_visible()
    page.keyboard.press("Shift")
    assert hint.is_visible()


def test_desktop_is_unchanged(open_view):
    page = open_view(VIEW, "desktop")
    assert not page.evaluate("() => JelliTouch.coarse()")
    assert not page.locator("#jt-list-button").is_visible()
    assert page.locator("aside").is_visible() and page.locator("#shell-help-button kbd").is_visible()


def test_long_press_shows_a_title_and_swallows_the_click(open_view):
    page = open_view(VIEW, "ipad-portrait")
    page.evaluate("""() => { const b = document.createElement('button'); b.id = 'jt-titled'; b.title = 'Explains what it does';
        b.textContent = 'Titled'; Object.assign(b.style, {position: 'fixed', left: '300px', top: '600px', zIndex: '1000'});
        window.__clicks = 0; b.onclick = () => window.__clicks++; document.body.append(b); }""")
    box = page.locator("#jt-titled").bounding_box()
    long_press(page, (box["x"] + box["width"] / 2, box["y"] + box["height"] / 2))
    pop = page.locator(".jt-pop")
    assert pop.is_visible() and pop.inner_text() == "Explains what it does"
    assert page.evaluate("() => window.__clicks") == 0
    tap(page, [(100, 900)])
    assert not pop.is_visible()
    page.locator("#jt-titled").tap()
    assert page.evaluate("() => window.__clicks") == 1


def test_info_button_reveals_its_text(open_view):
    page = open_view(VIEW, "ipad-portrait")
    page.evaluate("""() => document.body.insertAdjacentHTML('beforeend',
        `<div style="position:fixed;left:300px;top:600px;z-index:1000">${JelliTouch.info('Durations are in milliseconds.', 'About durations')}</div>`)""")
    info = page.get_by_role("button", name="About durations")
    assert small_targets(page, "body > div:last-of-type") == []
    info.tap()
    assert page.locator(".jt-pop").inner_text() == "Durations are in milliseconds." and info.get_attribute("aria-expanded") == "true"
    info.tap()
    assert not page.locator(".jt-pop").is_visible() and info.get_attribute("aria-expanded") == "false"


def test_toast_waits_while_touched(open_view):
    page = open_view(VIEW, "ipad-portrait")
    page.evaluate("() => JelliShell.notify('Touched toast', {timeout: 400})")
    toast = page.locator(".shell-toast", has_text="Touched toast")
    toast.locator("p").first.tap()
    page.wait_for_timeout(900)
    assert toast.is_visible()


def test_help_lists_touch_and_pencil(open_view):
    page = open_view(VIEW, "ipad-portrait")
    page.evaluate("() => JelliTouch.registerGestures([{gesture: 'Two-finger tap', description: 'Undo'}])")
    page.evaluate("() => JelliShell.openHelp()")
    dialog = page.locator("dialog[open]")
    assert dialog.get_by_role("heading", name="Shortcuts and gestures").is_visible()
    section = dialog.locator("section", has=page.get_by_role("heading", name="Touch and Pencil"))
    assert "Two-finger tap" in section.inner_text() and "Long press a control" in section.inner_text()


def test_pencil_only_persists_and_notifies(open_view):
    page = open_view(VIEW, "ipad-portrait")
    assert page.evaluate("() => JelliTouch.pencilOnly()") is False
    assert page.evaluate("() => { let n = 0; JelliTouch.onPencilOnly(on => { n += on ? 1 : 10; }); JelliTouch.setPencilOnly(true);"
                         " JelliTouch.setPencilOnly(true); return n; }") == 1
    page.reload()
    page.wait_for_function("() => typeof JelliTouch !== 'undefined'")
    assert page.evaluate("() => JelliTouch.pencilOnly()") is True
    page.evaluate("() => JelliTouch.setPencilOnly(false)")


def test_pencil_only_offered_once(open_view):
    page = open_view(VIEW, "ipad-portrait")
    page.evaluate("() => JelliTouch.offerPencilOnly()")
    offer = page.get_by_role("button", name="Turn on Pencil only")
    offer.tap()
    assert page.evaluate("() => JelliTouch.pencilOnly()") is True
    assert not offer.is_visible()
    page.evaluate("() => { JelliTouch.setPencilOnly(false); JelliTouch.offerPencilOnly(); }")
    assert page.locator(".shell-toast", has_text="Using an Apple Pencil").count() == 0
