"""Mint (baby) and Lilac (grown) jelly creatures built from one body and face kit.

Every pose shares the same eye, cheek, mouth and gloss vocabulary so frames read
as one character. Bodies are row spans; poses vary the spans and the face only.
"""
from pixel_kit import disc, outline, paint, shade, spans

# Half-widths per row, top to bottom, for the baby dome.
BABY = {
    "idle-a": (14, [3, 6, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11, 11, 11, 10]),
    "idle-b": (16, [4, 7, 9, 10, 10, 11, 11, 11, 12, 12, 12, 12, 11]),
    "asleep": (19, [4, 7, 9, 10, 11, 11, 12, 12, 12, 11]),
    "happy": (12, [3, 5, 7, 8, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 10]),
    "unwell": (16, [3, 6, 8, 9, 10, 10, 11, 11, 11, 12, 12, 12, 11]),
}
BABY.update({"eating": BABY["idle-a"], "content": BABY["idle-b"], "curious": BABY["idle-a"]})


def lean(rows, top, amount):
    """Shift upper rows sideways so the body leans; bottom rows stay planted."""
    out = []
    for i, hw in enumerate(rows):
        shift = round(amount * (1 - i / (len(rows) - 1)) ** 1.5)
        out.append((16 - hw + shift, 15 + hw + shift))
    return out


def eyes(s, row, gap, kind, look=0, cx=16):
    """Eyes are 2x3 ink with a white catchlight; closed variants are 3px arcs."""
    for side in (-1, 1):
        x = cx - gap - 2 if side < 0 else cx + gap
        x += look
        if kind == "open":
            paint(s, [(x + i, row + j) for i in range(2) for j in range(3)], "ink")
            s[(x + (1 if look > 0 else 0), row)] = "white"
        elif kind == "happy":  # ^ ^
            paint(s, [(x - 1, row + 2), (x, row + 1), (x + 1, row + 1), (x + 2, row + 2)], "ink")
        elif kind == "closed":  # u u, content and asleep
            paint(s, [(x - 1, row + 1), (x, row + 2), (x + 1, row + 2), (x + 2, row + 1)], "ink")
        elif kind == "tired":  # heavy lids
            paint(s, [(x - 1, row + 1), (x, row + 1), (x + 1, row + 1), (x + 2, row + 1)], "ink")
            paint(s, [(x, row + 2), (x + 1, row + 2)], "ink")


def cheeks(s, row, gap, color="coral", cx=16):
    for x in (cx - gap - 4, cx - gap - 3, cx + gap + 2, cx + gap + 3):
        s[(x, row)] = color


def mouth(s, row, kind, cx=16, look=0):
    x = cx + look
    shapes = {
        "smile": [(x - 2, row), (x + 1, row), (x - 1, row + 1), (x, row + 1)],
        "small": [(x - 1, row + 1), (x, row + 1)],
        "open": [(x - 1, row), (x, row), (x - 2, row + 1), (x + 1, row + 1), (x - 1, row + 2), (x, row + 2)],
        "chomp": [(x - 2, row), (x - 1, row), (x, row), (x + 1, row)],
        "o": [(x - 1, row), (x, row), (x - 1, row + 1), (x, row + 1)],
        "wavy": [(x - 2, row + 1), (x - 1, row), (x, row + 1), (x + 1, row)],
    }
    paint(s, shapes[kind], "ink")
    if kind == "open":
        paint(s, [(x - 1, row + 1), (x, row + 1)], "rose")


def gloss(s, mask, light="cream", drop=2):
    """A small curved gloss near the top-left of the silhouette."""
    top = min(y for _, y in mask)
    row = top + drop
    left = min(x for x, y in mask if y == row) + 2
    for p, c in (((left + 1, row), "white"), ((left + 2, row), "white"), ((left, row + 1), light), ((left + 1, row + 1), light)):
        if p in mask:
            s[p] = c


def sweat(s, x, y):
    """Teardrop just outside the silhouette; the outline pass wraps it in ink."""
    paint(s, [(x, y), (x, y + 1), (x + 1, y + 1), (x, y + 2), (x + 1, y + 2)], "white")


def berry(s, x, y):
    paint(s, [(x, y), (x + 1, y), (x - 1, y + 1), (x, y + 1), (x + 1, y + 1), (x + 2, y + 1), (x, y + 2), (x + 1, y + 2)], "coral")
    paint(s, [(x + 1, y + 2), (x + 2, y + 1)], "rose")
    s[(x, y)] = "cream"
    paint(s, [(x, y - 1), (x + 1, y - 2)], "sea")


def baby(pose):
    top, rows = BABY[pose]
    body_rows = lean(rows, top, 2) if pose == "curious" else rows
    mask = spans(body_rows, top)
    s = shade({}, mask, "mint")
    gloss(s, mask)
    height = len(rows)
    eye_row = top + max(4, height * 2 // 5 + (1 if height > 14 else 0))
    look = 1 if pose == "curious" else 0
    kind = {"happy": "happy", "content": "closed", "asleep": "closed", "unwell": "tired"}.get(pose, "open")
    eyes(s, eye_row, 3, kind, look)
    cheeks(s, eye_row + 3, 3, "haze" if pose == "unwell" else "coral")
    mouth_kind = {"happy": "open", "eating": "chomp", "curious": "o", "asleep": "small", "unwell": "wavy"}.get(pose, "smile")
    mouth(s, eye_row + 3, mouth_kind, look=look)
    if pose == "eating":
        berry(s, 15, eye_row + 4)
    if pose == "unwell":
        sweat(s, max(x for x, y in mask if y == top + 1) + 2, top + 1)
    return outline(s)


GROWN = {
    "idle-a": (11, [6, 8, 9, 10, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11]),
    "idle-b": (13, [7, 9, 10, 11, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12]),
    "asleep": (17, [7, 9, 11, 12, 12, 13, 13, 13, 13, 13, 13, 12]),
    "happy": (9, [5, 7, 8, 9, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11]),
    "unwell": (13, [6, 8, 9, 10, 11, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 12]),
}
GROWN.update({"eating": GROWN["idle-a"], "content": GROWN["idle-b"], "curious": GROWN["idle-a"]})


def grown(pose):
    top, rows = GROWN[pose]
    body_rows = lean(rows, top, 2) if pose == "curious" else rows
    mask = spans(body_rows, top)
    bottom = top + len(rows) - 1
    # Jelly feet: the last row splits into three soft drips.
    left, right = min(x for x, y in mask if y == bottom), max(x for x, y in mask if y == bottom)
    mask -= {(x, bottom) for x in range(left, right + 1) if x in (left + 5, left + 6, right - 6, right - 5) or x in (left, right)}
    # Round ears sit on the shoulders and droop when sleepy or unwell.
    shift = 2 if pose == "curious" else 0
    ear_y = top + (2 if pose in ("asleep", "unwell") else 0)
    spread = 7 if pose == "asleep" else 6
    ears = disc(16 - spread + shift - 0.0, ear_y, 3.2) | disc(16 + spread + shift, ear_y, 3.2)
    mask |= ears
    s = shade({}, mask, "haze", light_rim=False)
    for ex in (16 - spread + shift, 16 + spread + shift):
        paint(s, [p for p in disc(ex, ear_y, 1.6) if p in mask], "lilac")
    gloss(s, mask, drop=4)
    eye_row = top + max(5, len(rows) * 2 // 5 + 1)
    look = 1 if pose == "curious" else 0
    kind = {"happy": "happy", "content": "closed", "asleep": "closed", "unwell": "tired"}.get(pose, "open")
    eyes(s, eye_row, 3, kind, look)
    cheeks(s, eye_row + 3, 3, "lilac" if pose == "unwell" else "coral")
    mouth_kind = {"happy": "open", "eating": "chomp", "curious": "o", "asleep": "small", "unwell": "wavy"}.get(pose, "smile")
    mouth(s, eye_row + 3, mouth_kind, look=look)
    if pose == "eating":
        berry(s, 15, eye_row + 4)
    if pose == "unwell":
        sweat(s, max(x for x, y in mask if y == top + 3) + 2, top + 3)
    return outline(s)


POSES = ("idle-a", "idle-b", "eating", "happy", "asleep", "unwell", "curious", "content")


def creatures():
    out = {}
    for pose in POSES:
        out[f"creatures/baby-{pose}.png"] = baby(pose)
        out[f"creatures/grown-{pose}.png"] = grown(pose)
    return out
