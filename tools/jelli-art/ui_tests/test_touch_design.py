"""RFC-007 T3: the game design views on an iPad. Behaviour (the Behaviour & size panel of the Creature view),
Reactions (the Behaviour view: states, repertoires and the stimulus simulator), Activities and Test in game are
driven with emulated touch (touch.py contexts) at iPad portrait, landscape and mini sizes. They must keep every control
at least 44 x 44 px (disabled ones too: the fixture serves read-only content), never scroll sideways, reveal their
tap-to-reveal help, step number fields with + and -, and switch layout with the view's own width.

Run: tools/jelli-art/ui_tests/run.sh -k touch_design
"""
import pytest

from conftest import TOUCH_VIEWPORTS, new_view

TOUCH_VIEWPORTS.setdefault("ipad-mini", [744, 1133])  # RFC-007's smallest target; the shared cases stay as they are
SIZES = ["ipad-portrait", "ipad-landscape", "ipad-mini"]
STACKED = {"ipad-portrait", "ipad-mini"}

# name -> (the mode tab to open, the view's tab inside it or None, the root element measured)
VIEWS = {
    "behaviour": ("creature", None, "#cr-behaviour"),
    "states": ("behaviour", "states", "#behaviour"),
    "repertoires": ("behaviour", "repertoires", "#behaviour"),
    "simulator": ("behaviour", "simulator", "#behaviour"),
    "activities": ("activities", None, "#activities"),
    "game": ("game", None, "#game"),
}

SMALL = """([root, minimum]) => {
    const sel = 'button, select, input:not([type=hidden]), textarea, [role=button], [role=tab], [tabindex="0"]';
    const out = [];
    for (const el of document.querySelector(root).querySelectorAll(sel)) {
      const r = el.getBoundingClientRect(), cs = getComputedStyle(el);
      if (!r.width || !r.height || cs.visibility === 'hidden' || el.closest('[hidden], .sr-only, .hidden')) continue;
      if (['checkbox', 'radio'].includes(el.type) && el.closest('label')) {
        const l = el.closest('label').getBoundingClientRect();
        if (l.width >= minimum && l.height >= minimum) continue;
      }
      if (r.width < minimum - 0.5 || r.height < minimum - 0.5) {
        const name = el.getAttribute('aria-label') || el.textContent.trim().slice(0, 30) || el.id || el.className;
        out.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''} "${name}" ${Math.round(r.width)}x${Math.round(r.height)}`);
      }
    }
    return out; }"""

OVERFLOW = """() => { const m = document.querySelector('main'), d = document.documentElement;
    return {page: d.scrollWidth - d.clientWidth, main: m.scrollWidth - m.clientWidth}; }"""


@pytest.fixture
def design(browser, studio):
    """design(name, size) -> a page at an iPad size showing that design view, settled. Contexts close afterwards."""
    contexts = []

    def opener(name, size):
        mode, tab, _root = VIEWS[name]
        context, page = new_view(browser, studio, "creature", size)
        contexts.append(context)
        page.on("dialog", lambda d: d.dismiss())
        return page, open_tab(page, mode, tab)

    yield opener
    for context in contexts:
        context.close()


def open_tab(page, mode, tab, wait=True):
    page.locator(f"#tab-{mode}").tap()
    if tab:
        page.locator(f"[data-tab={tab}]").tap()
    if wait:
        page.wait_for_timeout(300)


def rect(page, selector):
    return page.evaluate("""s => { const r = document.querySelector(s).getBoundingClientRect();
        return {left: r.left, right: r.right, top: r.top, bottom: r.bottom, width: r.width, height: r.height}; }""", selector)


def pinned(page, selector):
    """Scroll main down and report how far the element's top is from main's top (about 0 while it is pinned)."""
    page.evaluate("() => { const m = document.querySelector('main'); m.scrollTop = Math.min(700, m.scrollHeight - m.clientHeight); }")
    page.wait_for_timeout(100)
    return rect(page, selector)["top"] - rect(page, "main")["top"], page.evaluate("document.querySelector('main').scrollTop")


@pytest.mark.parametrize("size", SIZES)
@pytest.mark.parametrize("name", VIEWS)
def test_controls_are_touch_sized_and_nothing_scrolls_sideways(design, name, size):
    page, _ = design(name, size)
    root = VIEWS[name][2]
    assert page.evaluate("() => matchMedia('(any-pointer: coarse)').matches")
    assert page.evaluate(SMALL, [root, 44]) == []
    over = page.evaluate(OVERFLOW)
    assert max(over.values()) <= 0, f"{name}@{size} scrolls sideways: {over}"


@pytest.mark.parametrize("size", SIZES)
def test_glyph_buttons_gain_words_on_touch(design, size):
    page, _ = design("repertoires", size)
    for word in ("Earlier", "Later", "Duplicate", "Remove"):
        assert page.locator(f"#bv-reactions .jd-w:text-is(' {word}')").first.is_visible()
    page, _ = design("behaviour", size)
    assert page.locator("#bh-rules .jd-w:text-is(' Earlier')").first.is_visible()
    assert page.locator("#bh-rules .jd-w:text-is(' Remove')").first.is_visible()


def test_words_stay_hidden_with_a_mouse(browser, studio):
    """Desktop pages keep their glyph-only buttons, steppers and help buttons."""
    context = browser.new_context(viewport={"width": 1440, "height": 900})
    try:
        page = context.new_page()
        page.goto(f"{studio}/#view=behaviour&key=creatures.grown-happy")
        page.wait_for_function("() => typeof D !== 'undefined' && D && D.assets && D.assets.length > 0")
        page.locator("[data-tab=repertoires]").click()
        assert not page.locator("#bv-reactions .jd-w").first.is_visible()
        assert page.locator("#bv-reactions .jd-s").count() > 0 and not page.locator("#bv-reactions .jd-s").first.is_visible()
        assert not page.locator(".jd-i .jt-info").first.is_visible()
    finally:
        context.close()


@pytest.mark.parametrize("name,hint", [("behaviour", "first condition that holds"), ("states", "scaled to about 40 px"),
                                       ("repertoires", "Weight is its share"), ("simulator", "Save behaviour"),
                                       ("game", "Render in engine")])
def test_help_reveals_by_tap(design, name, hint):
    page, _ = design(name, "ipad-portrait")
    button = page.locator(f"{VIEWS[name][2]} .jd-i .jt-info").first if name != "simulator" else page.locator(".bar .jd-i .jt-info")
    assert button.get_attribute("aria-expanded") == "false"
    button.tap()
    pop = page.locator("#jt-pop")
    pop.wait_for(state="visible")
    assert hint in pop.inner_text()
    assert button.get_attribute("aria-expanded") == "true"
    page.locator("main").tap(position={"x": 5, "y": 5})  # a tap elsewhere closes it
    pop.wait_for(state="detached")


def step(page, minus, plus, field):
    """Tap + twice and - once on a stepper pair: returns the values after each tap."""
    values = []
    for button in (plus, plus, minus):
        page.locator(button).tap()
        page.wait_for_timeout(120)
        values.append(page.locator(field).input_value())
    return values


def test_steppers_step_number_fields(design):
    page, _ = design("behaviour", "ipad-portrait")
    start = int(page.locator("#bh-pet").input_value())
    assert step(page, "[aria-label='Decrease Pet ID']", "[aria-label='Increase Pet ID']", "#bh-pet") == [str(start + 1), str(start + 2), str(start + 1)]
    assert page.evaluate("document.querySelector('#bh-pet').value") == str(start + 1)

    page, _ = design("simulator", "ipad-portrait")  # the simulator's inputs stay live on read-only content
    mood = int(page.locator("[data-sim=mood]").input_value())
    got = step(page, "[aria-label='Decrease Mood']", "[aria-label='Increase Mood']", "[data-sim=mood]")
    assert got == [str(mood + 1), str(mood + 2), str(mood + 1)]
    assert page.evaluate("store.get('behaviour-sim').mood") == str(mood + 1)  # the change event reached the view

    page, _ = design("repertoires", "ipad-portrait")  # read-only: the steppers are disabled with their fields
    assert page.locator("#bv-reactions .jd-s").first.is_disabled()

    page, _ = design("activities", "ipad-portrait")
    base = int(page.locator("[data-field=duration_s]").input_value())
    page.locator("label:has([data-field=duration_s]) .jd-s").nth(1).tap()
    page.wait_for_timeout(150)
    assert int(page.locator("[data-field=duration_s]").input_value()) == base + 1
    assert "Unsaved" in page.locator("#activities [role=status]").inner_text()  # the view saw the edit

    page, _ = design("game", "ipad-portrait")
    page.locator("[aria-label='Increase Potty urge']").tap()
    page.wait_for_timeout(100)
    assert page.locator("#gp-potty").input_value() == "10"
    assert page.evaluate("store.get('game-scenario').potty") == 10
    page.locator("[aria-label='Decrease Potty urge']").tap()
    assert page.locator("#gp-potty").input_value() == "0"


def two_columns(page, name):
    """True when the view's list and inspector sit side by side."""
    pairs = {"states": ("#bv-states", "#bv-state"), "repertoires": ("#bv-reps", "#bv-reactions"),
             "behaviour": (".bh-grid > :nth-child(1)", ".bh-grid > :nth-child(2)"), "activities": (".ac-list", ".ac-fields"),
             }
    left, right = pairs[name]
    a, b = rect(page, left), rect(page, right)
    return a["right"] <= b["left"] + 1 and abs(a["top"] - b["top"]) < 400


@pytest.mark.parametrize("name", ["behaviour", "states", "repertoires", "activities"])
@pytest.mark.parametrize("size", SIZES)
def test_layout_switches_columns_with_the_views_own_width(design, name, size):
    page, _ = design(name, size)
    assert two_columns(page, name) == (size not in STACKED), f"{name}@{size}"


def test_game_form_and_result_switch_columns(design):
    wide, _ = design("game", "ipad-landscape")
    assert rect(wide, "#gp-form")["width"] < 400
    tall, _ = design("game", "ipad-portrait")
    assert rect(tall, "#gp-form")["width"] > 600


@pytest.mark.parametrize("name,pin", [("states", ".bv-preview"), ("simulator", ".bv-preview"), ("activities", ".ac-grid > aside")])
def test_preview_is_pinned_when_stacked(design, name, pin):
    page, _ = design(name, "ipad-portrait")
    assert rect(page, pin)["top"] < rect(page, "main")["top"] + 400  # on top of the inspector, not below it
    # The shorter header (UX M2-A) leaves these views too little to scroll at 1180 px tall to reach the point where the preview sticks.
    page.set_viewport_size({"width": 820, "height": 800})
    page.wait_for_timeout(100)
    offset, scrolled = pinned(page, pin)
    assert scrolled > 100 and 0 <= offset <= 16, f"{name}: scrolled {scrolled}px and the preview is {offset}px from the top"


PNG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="


def mock_engine(page):
    """Test in game is unavailable on the fixture host (no cmake): answer as an engine that renders three frames."""
    def handle(route):
        if route.request.method == "GET":
            data = route.fetch().json()
            data["available"] = True
            return route.fulfill(json=data)
        return route.fulfill(json={"ok": True, "frames": [PNG] * 3, "alt": ["frame 1", "frame 2", "frame 3"], "times_ms": [0, 200, 400],
                                   "scenario": {"step_ms": 200}, "rebuilt": False, "last_build": {},
                                   "engine": {"form_name": "x", "pose": 0, "behavior": "", "asleep": False, "activity": "none", "moment": 0,
                                              "mess": False, "clock_minute": 540, "night": False, "needs": [1, 2, 3, 4, 5], "potty": 0}})
    page.route("**/api/game-preview", handle)


@pytest.mark.parametrize("size", SIZES)
def test_frame_buttons_replace_the_comma_and_period_keys(browser, studio, size):
    context, page = new_view(browser, studio, "creature", size)
    try:
        mock_engine(page)
        open_tab(page, "game", None)
        page.locator("#gp-render").tap()
        page.locator("#gp-big").wait_for()
        label = page.locator("#gp-frame-label")
        assert label.inner_text().startswith("1/3")
        page.locator("#gp-next").tap()
        assert label.inner_text().startswith("2/3")
        page.locator("#gp-prev").tap()
        page.locator("#gp-prev").tap()
        assert label.inner_text().startswith("3/3")  # wraps like the key does
        assert page.evaluate(SMALL, ["#game", 44]) == []
        over = page.evaluate(OVERFLOW)
        assert max(over.values()) <= 0, over
        if size in STACKED:  # the rendered frame and its controls stay in view while the form scrolls
            offset, scrolled = pinned(page, ".gp-pin")
            assert scrolled > 100 and 0 <= offset <= 16, (offset, scrolled)
    finally:
        context.close()
