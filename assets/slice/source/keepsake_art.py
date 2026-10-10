"""Hand-drawn redraws for keepsakes and effects that did not read at device scale."""
import math

from pixel_kit import disc, outline, paint, shade, spans


def mirror(rows, width=32):
    return [(width - 1 - r, width - 1 - l) for l, r in rows]


def butterfly():
    s = {}
    for cx in (9.5, 22.5):  # upper wings
        shade(s, disc(cx, 11.5, 6.2), "lilac", light_rim=False, shadow_rows=1)
        paint(s, disc(cx - (1 if cx < 16 else -1), 10.5, 1.6), "cream")
    for cx in (11.0, 21.0):  # lower wings
        shade(s, disc(cx, 21.0, 4.3), "coral", light_rim=False, shadow_rows=1)
        paint(s, disc(cx, 21.0, 1.6), "gold")
    body = spans([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1], 11)
    paint(s, body, "violet")
    paint(s, [(15, 11), (15, 12)], "lilac")
    paint(s, disc(16, 9, 1.6), "violet")
    paint(s, [(14, 6), (13, 5), (12, 4), (17, 6), (18, 5), (19, 4)], "violet")
    paint(s, [(11, 4), (20, 4), (11, 3), (20, 3)], "gold")
    return outline(s)


def friendship_bow():
    s = {}
    left = [(5, 8), (4, 10), (4, 12), (4, 13), (4, 14), (4, 14), (4, 13), (4, 12), (5, 10), (6, 8)]
    for rows in (left, mirror(left)):
        shade(s, spans(rows, 8), "coral", light_rim=False, shadow_rows=1)
    paint(s, [(6, 10), (7, 10), (5, 11), (6, 11)], "cream")
    paint(s, [(25, 10), (26, 10)], "cream")
    paint(s, [(11, 12), (12, 13), (12, 14), (11, 15)], "rose")  # folds
    paint(s, [(20, 12), (19, 13), (19, 14), (20, 15)], "rose")
    tail = [(13, 15), (12, 14), (12, 14), (11, 13), (11, 13), (10, 12), (10, 12), (9, 11), (9, 12)]
    for rows in (tail, mirror(tail)):
        shade(s, spans(rows, 18), "coral", light_rim=False, shadow_rows=1)
    paint(s, [(10, 26), (21, 26)], "ink")  # notched tail ends
    knot = spans([(14, 17)] * 6, 10)
    shade(s, knot, "coral", light_rim=False, shadow_rows=1, deep_rows=0)
    paint(s, [(14, 11), (15, 11)], "cream")
    paint(s, [(13, y) for y in range(10, 16)] + [(18, y) for y in range(10, 16)], "ink")
    return outline(s)


def arc_band(cx, cy, r0, r1, y_max):
    return {(x, y) for y in range(0, y_max + 1) for x in range(0, 32)
            if r0 < math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= r1}


def rainbow_seed():
    s = {}
    for (r0, r1), color in zip(((7.5, 9.5), (5.5, 7.5), (3.5, 5.5)), ("coral", "gold", "mint")):
        paint(s, arc_band(16, 14, r0, r1, 13), color)
    seed = spans([2, 4, 5, 6, 6, 7, 7, 7, 7, 6, 5, 3], 15)
    shade(s, seed, "gold", light_rim=False, shadow_rows=2)
    paint(s, [(12, 18), (12, 19), (13, 17)], "cream")
    paint(s, [(15, 13), (16, 13), (16, 14), (15, 14)], "sea")  # sprout
    paint(s, [(17, 12), (18, 12), (18, 11)], "mint")
    paint(s, [(14, 12), (13, 12), (13, 11)], "mint")
    return outline(s)


def rainbow_effect():
    s = {}
    for (r0, r1), color in zip(((4.6, 6.6), (2.6, 4.6)), ("coral", "gold")):
        paint(s, {(x, y) for x, y in arc_band(8, 11, r0, r1, 10) if x < 16}, color)
    paint(s, {(x, y) for x, y in arc_band(8, 11, 1.4, 2.6, 10) if x < 16}, "mint")
    for cx in (3, 13):
        paint(s, disc(cx, 11.5, 1.8), "cream")
        s[(cx - 1, 11)] = "white"
    return outline(s, size=16)


def keepsakes():
    return {"prizes/butterfly.png": butterfly(), "prizes/friendship-bow.png": friendship_bow(),
            "prizes/rainbow-seed.png": rainbow_seed(), "effects/rainbow.png": rainbow_effect()}
