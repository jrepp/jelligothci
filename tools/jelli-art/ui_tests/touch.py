"""Pen and finger input for the UI tests (RFC-007), sent through the Chrome DevTools Protocol so the page sees
real PointerEvents: Input.dispatchMouseEvent with pointerType "pen" for an Apple Pencil, and
Input.dispatchTouchEvent with several touchPoints for fingers (each finger gets its own pointerId).

Open the page at a touch size, such as open_view(view, "ipad-portrait"), so the context has touch enabled.
Coordinates are CSS pixels in the viewport. Chromium releases every finger on touch_end(), so lift all fingers
together; there is no single-finger lift. Waits are real time: the recogniser in touch.js times its thresholds
with performance.now(), and thresholds(page) returns them (JelliTouch.T).

    pen_stroke(page, [(10, 10), (40, 10)])          a Pencil stroke through the points
    touch_start(page, [(x1, y1), (x2, y2)])          fingers down (ids 1, 2, ... by position in the list)
    touch_move(page, [(x1, y1), (x2, y2)])           the same fingers, moved
    touch_end(page)                                 every finger up
    tap(page, [(x1, y1), (x2, y2)])                 a quick multi-finger (or one-finger) tap
    long_press(page, (x, y))                        one finger held past the long-press time
    pinch(page, (cx, cy), 100, 200)                 two fingers spreading from 100 to 200 px apart
    pan(page, (cx, cy), (dx, dy))                   two fingers dragged together
    small_targets(page, "main")                     visible controls under 44 x 44 CSS px
    sideways_overflow(page)                         px the page scrolls sideways (0 when it fits)
"""
_SESSIONS = {}
_DOWN = set()  # ids of pages with fingers down
STEPS = 6  # moves per drag, so the page sees a path rather than a jump


def cdp(page):
    """One CDP session per page."""
    session = _SESSIONS.get(id(page))
    if session is None:
        session = _SESSIONS[id(page)] = page.context.new_cdp_session(page)
    return session


def thresholds(page):
    return page.evaluate("() => ({...JelliTouch.T})")


def _pen(page, kind, x, y, pressed):
    cdp(page).send("Input.dispatchMouseEvent", {"type": kind, "x": x, "y": y, "button": "left", "buttons": 1 if pressed else 0,
                                                "clickCount": 1, "pointerType": "pen", "force": 0.5 if pressed else 0})


def pen_down(page, x, y):
    _pen(page, "mousePressed", x, y, True)


def pen_move(page, x, y, pressed=True):
    """A pen move; pressed=False is a hover (no contact)."""
    _pen(page, "mouseMoved", x, y, pressed)


def pen_up(page, x, y):
    _pen(page, "mouseReleased", x, y, False)


def pen_stroke(page, points, hold_ms=0):
    """Pen down at the first point, through the rest (each segment in STEPS moves), then up."""
    (x, y), *rest = points
    pen_down(page, x, y)
    for nx, ny in rest:
        for i in range(1, STEPS + 1):
            pen_move(page, x + (nx - x) * i / STEPS, y + (ny - y) * i / STEPS)
        x, y = nx, ny
    if hold_ms:
        page.wait_for_timeout(hold_ms)
    pen_up(page, x, y)


def _points(points):
    return [{"x": x, "y": y, "id": i + 1, "radiusX": 4, "radiusY": 4, "force": 1} for i, (x, y) in enumerate(points)]


def touch_start(page, points):
    """Fingers down. Pass every finger that is down, earlier ones first: a longer list adds fingers."""
    cdp(page).send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": _points(points)})
    _DOWN.add(id(page))


def touch_move(page, points):
    cdp(page).send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": _points(points)})


def touch_end(page):
    """Every finger up; nothing when none is down."""
    if id(page) in _DOWN:
        _DOWN.discard(id(page))
        cdp(page).send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})


def drag(page, start, end, steps=STEPS):
    """Move the fingers in start to the positions in end, in steps."""
    for i in range(1, steps + 1):
        touch_move(page, [(sx + (ex - sx) * i / steps, sy + (ey - sy) * i / steps) for (sx, sy), (ex, ey) in zip(start, end)])


def tap(page, points, hold_ms=40):
    touch_start(page, points)
    page.wait_for_timeout(hold_ms)
    touch_end(page)


def long_press(page, point, hold_ms=None):
    """One finger held still for the long-press time plus a margin (or hold_ms), then lifted."""
    touch_start(page, [point])
    page.wait_for_timeout(hold_ms if hold_ms is not None else thresholds(page)["LONG_PRESS_MS"] + 150)
    touch_end(page)


def pinch(page, centre, from_gap, to_gap, lift=True):
    """Two fingers on a horizontal line through centre, from from_gap to to_gap px apart."""
    cx, cy = centre
    start = [(cx - from_gap / 2, cy), (cx + from_gap / 2, cy)]
    touch_start(page, start)
    drag(page, start, [(cx - to_gap / 2, cy), (cx + to_gap / 2, cy)])
    if lift:
        touch_end(page)


def pan(page, centre, delta, gap=100, lift=True):
    """Two fingers gap px apart around centre, dragged together by delta."""
    (cx, cy), (dx, dy) = centre, delta
    start = [(cx - gap / 2, cy), (cx + gap / 2, cy)]
    touch_start(page, start)
    drag(page, start, [(x + dx, y + dy) for x, y in start])
    if lift:
        touch_end(page)


def small_targets(page, root="body", minimum=44):
    """Visible, enabled interactive elements under root smaller than minimum x minimum CSS px, as descriptions.
    Elements inside [data-touch-surface] (canvases that take gestures) and inline links in text are skipped."""
    return page.evaluate("""([root, minimum]) => {
        const sel = 'button, select, input:not([type=hidden]), textarea, [role=button], [role=tab], [role=slider], [role=checkbox], [tabindex="0"]';
        const out = [];
        for (const el of document.querySelector(root)?.querySelectorAll(sel) || []) {
          if (el.closest('[data-touch-surface], [hidden], .sr-only') || el.disabled) continue;
          const r = el.getBoundingClientRect(), cs = getComputedStyle(el);
          if (!r.width || !r.height || cs.visibility === 'hidden' || cs.display === 'none') continue;
          if (['checkbox', 'radio'].includes(el.type) && el.closest('label')) {
            const l = el.closest('label').getBoundingClientRect();
            if (l.width >= minimum && l.height >= minimum) continue;
          }
          if (r.width < minimum || r.height < minimum) {
            const name = el.getAttribute('aria-label') || el.textContent.trim().slice(0, 30) || el.id || el.className;
            out.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''} "${name}" ${Math.round(r.width)}x${Math.round(r.height)}`);
          }
        }
        return out; }""", [root, minimum])


def sideways_overflow(page):
    return page.evaluate("() => Math.max(0, document.documentElement.scrollWidth - document.documentElement.clientWidth)")
