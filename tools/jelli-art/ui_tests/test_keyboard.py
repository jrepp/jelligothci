"""Keyboard smoke tests: the studio can be reached and driven without a pointer."""

FOCUSED = """() => { const el = document.activeElement; if (!el || el === document.body) return null;
    const style = getComputedStyle(el);
    return {tag: el.tagName.toLowerCase(), role: el.getAttribute('role'), id: el.id,
            visible: el.matches(':focus-visible') && (style.outlineStyle !== 'none' && parseFloat(style.outlineWidth) > 0
                     || style.boxShadow !== 'none')}; }"""


def tab_until(page, match, limit=80):
    """Press Tab until match(focused) is true; returns every focused element on the way."""
    seen = []
    for _ in range(limit):
        page.keyboard.press("Tab")
        focused = page.evaluate(FOCUSED)
        if focused:
            seen.append(focused)
            if match(focused):
                return seen
    raise AssertionError(f"Tab never reached the target; focused: {[f['id'] or f['tag'] for f in seen]}")


def test_tab_reaches_mode_tabs_and_paint_canvas(open_view):
    page = open_view("paint-creature", "desktop")
    page.locator("body").focus()
    seen = tab_until(page, lambda f: f["role"] == "tab")
    assert seen[-1]["visible"], "the focused mode tab has no visible focus indicator"
    seen = tab_until(page, lambda f: f["tag"] == "canvas" and f["role"] == "application")
    assert seen[-1]["visible"], "the focused canvas has no visible focus indicator"


def test_shortcuts_dialog_opens_and_escape_closes(open_view):
    page = open_view("review-detail", "desktop")
    page.locator("body").focus()
    page.keyboard.press("?")
    dialog = page.locator("dialog[open]")
    dialog.wait_for(state="visible")
    assert dialog.count() == 1
    page.keyboard.press("Escape")
    dialog.wait_for(state="detached")
