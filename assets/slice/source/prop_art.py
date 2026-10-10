"""24px props drawn with the shared outline and ramp rules."""
from pixel_kit import disc, outline, paint, shade, spans


def rect(x0, y0, x1, y1):
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def berry(s, cx, cy):
    shade(s, disc(cx, cy, 2.3), "coral", light_rim=False, shadow_rows=1)
    s[(int(cx) - 1, int(cy) - 1)] = "white"
    paint(s, [(int(cx), int(cy) - 3), (int(cx) + 1, int(cy) - 4)], "sea")


def food_bowl():
    s = {}
    for cx, cy in ((8, 11), (16, 11), (12, 9)):
        berry(s, cx, cy)
    bowl = spans([(3, 20), (3, 20), (4, 19), (5, 18), (6, 17), (7, 16)], 13, cx=12)
    shade(s, bowl, "sea", light_rim=False, shadow_rows=1)
    paint(s, [(x, 13) for x in range(3, 21)], "mint")
    paint(s, [(x, 14) for x in range(5, 9)], "foam")
    paint(s, [(x, 19) for x in range(9, 15)], "teal")
    return outline(s, size=24)


def ribbon_box(s, top):
    box = rect(4, top + 3, 19, 20)
    shade(s, box, "mint", light_rim=False, shadow_rows=1)
    paint(s, [(x, top + 3) for x in range(4, 20)], "sea")  # lid shadow line
    paint(s, [(x, y) for x in (11, 12) for y in range(top + 3, 21)], "coral")
    paint(s, [(12, y) for y in range(top + 3, 21)], "rose")
    paint(s, [(5, y) for y in range(top + 5, 19)], "foam")


def bow(s, cx, top):
    loops = spans([(cx - 5, cx - 2), (cx - 6, cx - 1), (cx - 6, cx - 1), (cx - 4, cx - 1)], top) | \
        spans([(cx + 1, cx + 4), (cx, cx + 5), (cx, cx + 5), (cx, cx + 3)], top)
    paint(s, loops, "coral")
    paint(s, [(cx - 4, top + 1), (cx - 3, top + 1), (cx + 2, top + 1), (cx + 3, top + 1)], "ink")
    paint(s, [(cx - 5, top + 1), (cx + 1, top + 1)], "white")
    paint(s, [(cx - 1, top + 2), (cx, top + 2), (cx - 1, top + 3), (cx, top + 3)], "rose")


def gift_closed():
    s = {}
    ribbon_box(s, 9)
    lid = rect(3, 9, 20, 11)
    shade(s, lid, "mint", light_rim=False, shadow_rows=1, deep_rows=0)
    paint(s, [(x, 9) for x in range(3, 21)], "foam")
    paint(s, [(x, y) for x in (11, 12) for y in (9, 10, 11)], "coral")
    bow(s, 12, 5)
    return outline(s, size=24)


def gift_open():
    s = {}
    ribbon_box(s, 10)
    paint(s, rect(5, 13, 18, 13), "teal")  # dark inside of the open box
    paint(s, [(x, y) for x in (11, 12) for y in (13,)], "rose")
    # Lid tipped up on the left edge.
    lid = {(x, y) for x in range(1, 12) for y in range(9 - (x - 1) // 3, 12 - (x - 1) // 3)}
    shade(s, lid, "mint", light_rim=False, shadow_rows=1, deep_rows=0)
    paint(s, [(x, y) for x, y in lid if x in (6, 7)], "coral")
    # Sparkles rising out of the box.
    paint(s, [(15, 3), (14, 4), (15, 4), (16, 4), (15, 5)], "gold")
    s[(15, 4)] = "white"
    paint(s, [(19, 7), (20, 8), (19, 9), (18, 8)], "gold")
    return outline(s, size=24)


def bed():
    s = {}
    paint(s, rect(2, 9, 3, 20), "tan")  # headboard post
    paint(s, rect(2, 9, 2, 20), "gold")
    frame = rect(4, 16, 21, 18)
    shade(s, frame, "gold", light_rim=False, shadow_rows=1)
    paint(s, rect(4, 19, 5, 20) | rect(20, 19, 21, 20), "tan")
    pillow = spans([(5, 9), (4, 10), (4, 10), (5, 9)], 11)
    shade(s, pillow, "cream", light_rim=False, shadow_rows=1, deep_rows=0)
    blanket = spans([(11, 18), (10, 20), (10, 21), (10, 21), (10, 21)], 11)
    shade(s, blanket, "lilac", light_rim=False, shadow_rows=1)
    paint(s, [(x, 12) for x in range(11, 17)], "haze")
    paint(s, [(x, 13) for x in (13, 16, 19)], "violet")
    return outline(s, size=24)


def props():
    return {"props/food-bowl.png": food_bowl(), "props/gift-closed.png": gift_closed(),
            "props/gift-open.png": gift_open(), "props/bed.png": bed()}
