# /// script
# requires-python = ">=3.12"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Regenerate the 37 high-resolution UI icons from the verbatim Gemini sheets.

The sheets beside this script (menus-sheet.jpg, icons-sheet.jpg,
meters-sheet.jpg, battery-sheet.jpg) are kept byte-for-byte as the generator
returned them; their prompts, model, aspect ratio and cell orders are in the
matching *.prompt.txt files. This script is the only step between those sheets
and the shipped PNGs under assets/slice/hires/{menus,icons,meters,battery}/, and
it is deterministic: running it twice writes byte-identical files.

    ./scripts/uv run --python 3.12 assets/slice/source/hires/ui/extract_ui.py
    ./scripts/uv run --python 3.12 assets/slice/source/hires/ui/extract_ui.py --review

Every sheet is a 4 x 4 grid of equal square cells on a flat chroma-key green.
Pipeline per sheet:
  1. Key colour. The key is the per-channel median of the clearly-green pixels,
     so the generator's slightly off-pure backdrop is matched exactly.
  2. Soft alpha. Each pixel's distance from the key is measured along the
     key's own chroma axis, normalised by brightness ("keyness",
     (G - max(R, B)) / G): the backdrop sits near 1, every subject colour (mint
     and teal included) at or below about 0.3, and a dark sliver of backdrop
     seen through a gap still reads as key. Two thresholds give a linear ramp:
     keyness <= SOLID_K is opaque, >= key - BG_MARGIN is transparent, in
     between is edge. (A plain RGB distance cannot tell a 50 % cream-over-green
     edge from solid mint and leaves a green fringe; the chroma axis can.)
  3. Despill. The generator's soft rim light picks up the green backdrop a few
     pixels deep, where the chroma ramp still calls the pixel solid. So every
     pixel within BAND of the backdrop gets a reference colour: the subject's
     own colour ASCENT steps further in along the depth field (thin parts such
     as stems and moon tips climb to their own centre line, never to a
     neighbouring object). The pixel is then fitted as a shaded reference over
     the key, C = a * L * F + (1 - a) * K (2 x 2 least squares in a and a*L),
     so edge shading is not mistaken for transparency; a plausible fit
     (0.4 <= L <= 1.8, small residual) lowers alpha to a. The key is un-mixed,
     F = K + (C - K) / a, faint pixels lean on the reference colour, and green
     over max(R, B) is capped at the reference's own (or neutral, whichever is
     more; warm gold/cream/coral references allow none). Mint and teal keep
     their green; nothing outside the edge band is touched.
  4. Islands. 8-connected components of the non-transparent mask whose solid
     area is below MIN_ISLAND pixels are discarded (JPEG speckle); every other
     component is assigned to the grid cell that holds its bounding-box centre,
     so a subject that slightly crosses a cell edge stays whole.
  5. Fit. The cell's components are cropped to their alpha bounds and resampled
     once with Lanczos in premultiplied space, through a sub-pixel source box,
     straight onto the exact-size canvas: the longest side fills FILL of the
     canvas, centred. Battery cells that share the battery body use one common
     scale (body width = BATTERY_BODY_FILL of the canvas) and are centred on the
     body, so the shell is identical across empty/low/medium/high/charging/
     unknown even where the "!", "?" or bolt overhangs it.
  6. Output. RGBA, 8-bit straight alpha, alpha < 8 cleared to fully transparent
     (RGB zeroed), written without metadata.
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
SLICE = HERE.parents[2]  # assets/slice
REPO = SLICE.parents[1]

GRID = 4
SOLID_K = 0.32        # keyness at or below this is fully opaque
BG_MARGIN = 0.12      # keyness within this of the key's own is fully transparent
SPILL_MARGIN = 64     # edge pixels may keep at most this much green over max(R, B)
MIN_ISLAND = 40       # components with fewer solid pixels are speckle
BAND = 7              # edge band (sheet pixels) checked for key contamination
ASCENT = 6            # steps inwards to the edge pixel's reference colour
HUE_TOLERANCE = 10    # extra green over max(R, B) an edge may have beyond its reference
WARM = 12             # R over B by more than this marks a warm (gold/cream/coral) colour
FIT_RESIDUAL = 0.25   # edge fit must explain the pixel to within this share of |C - K|
ALPHA_FLOOR = 8       # output alpha below this becomes 0
FILL = 0.85           # longest side of the subject / canvas side
BATTERY_BODY_FILL = 0.90

# name -> zero-based cell index in reading order (row * 4 + column)
SHEETS = {
    "menus": {
        "sheet": "menus-sheet.jpg", "size": 96,
        "cells": {"food": 0, "care": 1, "rest": 2, "collection": 3,
                  "more": 4, "settings": 5, "moments": 6, "gifts": 7,
                  "breakfast": 8, "tea": 9, "outing": 10, "movie": 11,
                  "close": 12},
    },
    "icons": {
        "sheet": "icons-sheet.jpg", "size": 96,
        "cells": {"basic-care": 0, "food": 1, "play": 2, "clean": 3,
                  "rest": 4, "wake": 5, "medicine": 6, "gift": 7,
                  "reward": 8, "inventory": 9, "back": 10, "confirm": 11},
    },
    "meters": {
        "sheet": "meters-sheet.jpg", "size": 64,
        "cells": {"satiety": 0, "energy": 1, "hygiene": 2, "amusement": 3,
                  "social": 4},
    },
    "battery": {
        "sheet": "battery-sheet.jpg", "size": 64,
        "cells": {"empty": 0, "low": 1, "medium": 2, "high": 3,
                  "charging": 4, "plugged": 5, "unknown": 6},
        "body": ("empty", "low", "medium", "high", "charging", "unknown"),
    },
}


def clamp8(v: float) -> int:
    return 0 if v <= 0 else 255 if v >= 255 else int(v + 0.5)


def keyness(r: int, g: int, b: int) -> float:
    """Distance towards the green key along its chroma axis, normalised by
    brightness: (G - max(R, B)) / G. The key sits near 1, every subject colour
    (mint and teal included) at or below about 0.3, and dark key-coloured
    slivers (a gap showing the backdrop in shadow) still read as key."""
    return (g - max(r, b)) / g if g else 0.0


def key_colour(rgb: bytes) -> tuple[int, int, int]:
    """Per-channel median of clearly key-coloured pixels."""
    hist = ([0] * 256, [0] * 256, [0] * 256)
    count = 0
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        if g - max(r, b) > 150:
            hist[0][r] += 1
            hist[1][g] += 1
            hist[2][b] += 1
            count += 1
    if count == 0:
        raise SystemExit("no key-coloured backdrop found")
    out = []
    for h in hist:
        acc, half = 0, count // 2
        for v, n in enumerate(h):
            acc += n
            if acc > half:
                out.append(v)
                break
    return out[0], out[1], out[2]


def key_sheet(path: Path) -> tuple[Image.Image, list[dict]]:
    """Return the keyed RGBA sheet and its kept components."""
    im = Image.open(path).convert("RGB")
    w, h = im.size
    if w != h:
        raise SystemExit(f"{path.name}: expected a square sheet, got {w}x{h}")
    rgb = im.tobytes()
    kr, kg, kb = key_colour(rgb)
    key_k = keyness(kr, kg, kb)
    bg_k = key_k - BG_MARGIN
    if bg_k <= SOLID_K + 0.2:
        raise SystemExit(f"{path.name}: key {kr, kg, kb} is too weak to separate")
    span = bg_k - SOLID_K
    n = w * h
    # Pass 1: background mask and the depth of every subject pixel: 4-connected
    # steps in from the backdrop, capped at BAND + ASCENT + 1.
    fg = bytearray(n)
    for p in range(n):
        i = p * 3
        if keyness(rgb[i], rgb[i + 1], rgb[i + 2]) < bg_k:
            fg[p] = 1
    max_depth = BAND + ASCENT + 1
    depth = bytearray(n)
    frontier = []
    for p in range(n):
        if not fg[p]:
            continue
        y, x = divmod(p, w)
        if (x == 0 or not fg[p - 1] or x == w - 1 or not fg[p + 1]
                or y == 0 or not fg[p - w] or y == h - 1 or not fg[p + w]):
            depth[p] = 1
            frontier.append(p)
    for level in range(2, max_depth + 1):
        nxt = []
        for p in frontier:
            y, x = divmod(p, w)
            for q, ok in ((p - 1, x > 0), (p + 1, x < w - 1), (p - w, y > 0), (p + w, y < h - 1)):
                if ok and fg[q] and not depth[q]:
                    depth[q] = level
                    nxt.append(q)
        frontier = nxt
    for p in range(n):
        if fg[p] and not depth[p]:
            depth[p] = max_depth

    def reference(p: int) -> int:
        """Climb the depth field ASCENT steps inwards (or to a local maximum):
        the subject's own clean colour just inside this edge pixel. Thin parts
        such as stems climb to their own centre line, never to a neighbour."""
        target = depth[p] + ASCENT
        q = p
        while depth[q] < target:
            y, x = divmod(q, w)
            want = depth[q] + 1
            for r, ok in ((q - w, y > 0), (q - 1, x > 0), (q + 1, x < w - 1), (q + w, y < h - 1)):
                if ok and depth[r] == want:
                    q = r
                    break
            else:
                break
        return q

    # Pass 2: soft alpha from the key-chroma ramp; edge-band pixels are then
    # refined against their reference colour and the key un-mixed out.
    out = bytearray(n * 4)
    alpha = bytearray(n)
    solid = bytearray(n)
    for p in range(n):
        if not fg[p]:
            continue
        i = p * 3
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        k = keyness(r, g, b)
        a = 1.0 if k <= SOLID_K else (bg_k - k) / span
        q = reference(p) if depth[p] <= BAND else p
        if q != p:
            j = q * 3
            fr0, fg0, fb0 = rgb[j], rgb[j + 1], rgb[j + 2]
            ref_s = fg0 - max(fr0, fb0)
            # Model the pixel as a shaded reference over the key,
            # C = a * L * F + (1 - a) * K, and solve the 2 x 2 least-squares
            # system for u = a * L and a. Shading (L) is thereby not mistaken
            # for transparency. The fit is trusted only when it is plausible
            # (0.4 <= L <= 1.8) and explains the pixel (small residual), so a
            # genuine colour change at the edge keeps the chroma-ramp alpha.
            dx, dy, dz = r - kr, g - kg, b - kb
            ff = fr0 * fr0 + fg0 * fg0 + fb0 * fb0
            fk = fr0 * kr + fg0 * kg + fb0 * kb
            kk = kr * kr + kg * kg + kb * kb
            fd = fr0 * dx + fg0 * dy + fb0 * dz
            kd = kr * dx + kg * dy + kb * dz
            det = ff * kk - fk * fk
            if det > 0:
                u = (kk * fd - fk * kd) / det
                af = (fk * fd - ff * kd) / det
                ex = u * fr0 - af * kr - dx
                ey = u * fg0 - af * kg - dy
                ez = u * fb0 - af * kb - dz
                res2 = ex * ex + ey * ey + ez * ez
                dd = dx * dx + dy * dy + dz * dz
                if res2 <= FIT_RESIDUAL * FIT_RESIDUAL * dd:
                    if af <= 0.02:
                        a = 0.0
                    elif 0.4 <= u / af <= 1.8:
                        a = min(a, af)
            if a <= 0.0:
                continue
            # Un-mix the key, then lean faint pixels on the reference colour
            # (dividing by a small alpha amplifies JPEG error), and finally
            # allow no more green-over-max(R, B) than the reference itself has
            # (or than a neutral highlight, G = max(R, B), whichever is more):
            # this removes green rim light without touching mint or teal and
            # without turning a pale rim on a red subject magenta.
            if a < 1.0:
                ur = kr + (r - kr) / a
                ug = kg + (g - kg) / a
                ub = kb + (b - kb) / a
                fr = ur * a + fr0 * (1 - a)
                fgv = ug * a + fg0 * (1 - a)
                fb = ub * a + fb0 * (1 - a)
            else:
                fr, fgv, fb = r, g, b
            fr, fgv, fb = clamp8(fr), clamp8(fgv), clamp8(fb)
            cap = min(max(ref_s + HUE_TOLERANCE, 0), SPILL_MARGIN)
            if fr0 > fb0 + WARM:
                # Warm subjects (gold, cream, coral) never carry green over
                # red; a thin warm part whose own centre caught spill still
                # gets clamped to neutral.
                cap = 0
            fgv = min(fgv, max(fr, fb) + cap)
            fgv = max(fgv, 0)
        else:
            if a < 1.0:
                fr = clamp8(kr + (r - kr) / a)
                fgv = clamp8(kg + (g - kg) / a)
                fb = clamp8(kb + (b - kb) / a)
                fgv = min(fgv, max(fr, fb) + SPILL_MARGIN)
            else:
                fr, fgv, fb = r, g, b
            if depth[p] <= BAND and fr > fb + WARM:
                # The centre line of a thin warm part (a moon tip, a handle)
                # is its own reference; it still never carries green over red.
                fgv = min(fgv, fr)
        av = clamp8(a * 255)
        if av == 0:
            continue
        o = p * 4
        if a >= 0.5:
            solid[p] = 1
        out[o], out[o + 1], out[o + 2], out[o + 3] = fr, fgv, fb, av
        alpha[p] = av

    # 8-connected components over the non-transparent mask.
    label = [0] * n
    comps: list[dict] = []
    for start in range(n):
        if not alpha[start] or label[start]:
            continue
        cid = len(comps) + 1
        label[start] = cid
        stack = [start]
        pixels = []
        area = 0
        x0, y0, x1, y1 = w, h, -1, -1
        while stack:
            p = stack.pop()
            pixels.append(p)
            area += solid[p]
            y, x = divmod(p, w)
            if x < x0: x0 = x
            if x > x1: x1 = x
            if y < y0: y0 = y
            if y > y1: y1 = y
            for dy in (-1, 0, 1):
                ny = y + dy
                if ny < 0 or ny >= h:
                    continue
                row = ny * w
                for dx in (-1, 0, 1):
                    nx = x + dx
                    if nx < 0 or nx >= w:
                        continue
                    q = row + nx
                    if alpha[q] and not label[q]:
                        label[q] = cid
                        stack.append(q)
        comps.append({"id": cid, "area": area, "bbox": (x0, y0, x1 + 1, y1 + 1),
                      "pixels": pixels})
    kept = []
    cell = w / GRID
    for c in comps:
        if c["area"] < MIN_ISLAND:
            for p in c["pixels"]:
                out[p * 4: p * 4 + 4] = b"\0\0\0\0"
            continue
        x0, y0, x1, y1 = c["bbox"]
        col = min(GRID - 1, int(((x0 + x1) / 2) // cell))
        row = min(GRID - 1, int(((y0 + y1) / 2) // cell))
        c["cell"] = row * GRID + col
        kept.append(c)
    return Image.frombytes("RGBA", (w, h), bytes(out)), kept


def cell_crop(sheet: Image.Image, comps: list[dict], index: int, name: str) -> Image.Image:
    """The cell's subject alone (other cells' components removed), cropped."""
    mine = [c for c in comps if c["cell"] == index]
    if not mine:
        raise SystemExit(f"{name}: no subject found in cell {index}")
    w, h = sheet.size
    mask = bytearray(w * h)
    for c in mine:
        for p in c["pixels"]:
            mask[p] = 255
    x0 = min(c["bbox"][0] for c in mine)
    y0 = min(c["bbox"][1] for c in mine)
    x1 = max(c["bbox"][2] for c in mine)
    y1 = max(c["bbox"][3] for c in mine)
    m = Image.frombytes("L", (w, h), bytes(mask))
    blank = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    return Image.composite(sheet, blank, m).crop((x0, y0, x1, y1))


def render(crop: Image.Image, size: int, scale: float, centre: tuple[float, float]) -> Image.Image:
    """Resample `crop` so `centre` lands on the canvas centre at `scale`."""
    pad = int(size / scale) + 4
    padded = Image.new("RGBA", (crop.width + 2 * pad, crop.height + 2 * pad), (0, 0, 0, 0))
    padded.paste(crop, (pad, pad))
    half = size / scale / 2
    cx, cy = centre[0] + pad, centre[1] + pad
    box = (cx - half, cy - half, cx + half, cy + half)
    out = padded.resize((size, size), Image.Resampling.LANCZOS, box=box)
    # Anything the box cut off would be visible clipping; refuse silently cropped art.
    full = crop.getchannel("A").getbbox()
    if full is not None:
        left = (full[0] - centre[0]) * scale + size / 2
        top = (full[1] - centre[1]) * scale + size / 2
        right = (full[2] - centre[0]) * scale + size / 2
        bottom = (full[3] - centre[1]) * scale + size / 2
        if left < -0.5 or top < -0.5 or right > size + 0.5 or bottom > size + 0.5:
            raise SystemExit("subject does not fit the canvas")
    return finish(out)


def finish(im: Image.Image) -> Image.Image:
    data = bytearray(im.tobytes())
    for o in range(0, len(data), 4):
        if data[o + 3] < ALPHA_FLOOR:
            data[o:o + 4] = b"\0\0\0\0"
    return Image.frombytes("RGBA", im.size, bytes(data))


def battery_body(crop: Image.Image) -> tuple[float, float, float]:
    """Width and centre of the horizontal battery shell (glyph overhang ignored)."""
    a = crop.getchannel("A")
    w, h = crop.size
    px = a.load()
    rows = []
    for y in range(h):
        xs = [x for x in range(w) if px[x, y] >= 128]
        if xs and xs[-1] - xs[0] + 1 >= 0.6 * w:
            rows.append((y, xs[0], xs[-1] + 1))
    if not rows:
        raise SystemExit("battery shell not found")
    top, bottom = rows[0][0], rows[-1][0] + 1
    left = min(r[1] for r in rows)
    right = max(r[2] for r in rows)
    return right - left, (left + right) / 2, (top + bottom) / 2


def extract(sheets_dir: Path, out_root: Path) -> dict[str, Image.Image]:
    results: dict[str, Image.Image] = {}
    for kind, spec in SHEETS.items():
        sheet, comps = key_sheet(sheets_dir / spec["sheet"])
        size = spec["size"]
        crops = {name: cell_crop(sheet, comps, idx, f"{kind}.{name}")
                 for name, idx in spec["cells"].items()}
        body = {}
        if "body" in spec:
            body = {name: battery_body(crops[name]) for name in spec["body"]}
            common = BATTERY_BODY_FILL * size / max(b[0] for b in body.values())
        (out_root / kind).mkdir(parents=True, exist_ok=True)
        for name, crop in crops.items():
            if name in body:
                _, cx, cy = body[name]
                img = render(crop, size, common, (cx, cy))
            else:
                scale = FILL * size / max(crop.size)
                img = render(crop, size, scale, (crop.width / 2, crop.height / 2))
            path = out_root / kind / f"{name}.png"
            img.save(path, format="PNG", optimize=False, compress_level=9)
            results[f"{kind}/{name}"] = img
    return results


# ---------------------------------------------------------------- review ----
# Review sheets show each icon at its on-screen size(s): ring icons on the
# game's ring button (a #4a4a52 disk with a lighter edge, icon size = disk
# diameter) and on near-black; the 32 px uses (menus more/close/settings, icons
# back, every battery icon on the home screen) on dark grey, near-black and the
# teal menu button, plus a 3x nearest-neighbour zoom of that 32 px render.
# Meters are also shown in the stat tile's plum well over its teal and pink fills.
DISK = (0x4A, 0x4A, 0x52)
DISK_EDGE = (0x73, 0x71, 0x73)
NEAR_BLACK = (0x10, 0x10, 0x10)
TEAL = (0x18, 0x79, 0x7B)
PAPER = (0x26, 0x26, 0x2C)
INK = (0xE8, 0xE8, 0xE8)
PAD = 8
SMALL = {("menus", "more"), ("menus", "close"), ("menus", "settings"), ("icons", "back")}
STAT_INK = (0x4A, 0x30, 0x4A)   # stat tile well (INK) ...
STAT_FILL = (0x18, 0x79, 0x7B)  # ... with its teal fill (score >= 25)
STAT_LOW = (0xFF, 0x8E, 0x9C)   # ... or its pink fill (score < 25)


def shrink(img: Image.Image, size: int) -> Image.Image:
    return finish(img.resize((size, size), Image.Resampling.LANCZOS))


def tile(img: Image.Image, bg: tuple[int, int, int], disk: bool = False) -> Image.Image:
    s = img.width
    t = Image.new("RGB", (s + 2 * PAD, s + 2 * PAD), NEAR_BLACK if disk else bg)
    if disk:
        d = ImageDraw.Draw(t)
        d.ellipse((PAD, PAD, PAD + s - 1, PAD + s - 1), fill=DISK_EDGE)
        e = max(1, round(s / 48))
        d.ellipse((PAD + e, PAD + e, PAD + s - 1 - e, PAD + s - 1 - e), fill=bg)
    t.paste(img, (PAD, PAD), img)
    return t


def stat_tile(img: Image.Image, fill: tuple[int, int, int], share: float) -> Image.Image:
    """The meter as drawn in the home stat tile: over the INK well with the
    vertical score fill rising from the bottom."""
    s = img.width
    t = Image.new("RGB", (s + 2 * PAD, s + 2 * PAD), STAT_INK)
    ImageDraw.Draw(t).rectangle((0, round((s + 2 * PAD) * (1 - share)), s + 2 * PAD, s + 2 * PAD), fill=fill)
    t.paste(img, (PAD, PAD), img)
    return t


def strip(tiles: list[Image.Image]) -> Image.Image:
    w = sum(t.width for t in tiles) + PAD * (len(tiles) - 1)
    h = max(t.height for t in tiles)
    out = Image.new("RGB", (w, h), PAPER)
    x = 0
    for t in tiles:
        out.paste(t, (x, (h - t.height) // 2))
        x += t.width + PAD
    return out


def card(kind: str, name: str, img: Image.Image) -> Image.Image:
    rows = []
    if kind in ("menus", "icons"):
        rows.append(strip([tile(img, DISK, disk=True), tile(img, NEAR_BLACK)]))
    elif kind == "meters":
        rows.append(strip([tile(img, DISK), tile(img, NEAR_BLACK),
                           stat_tile(img, STAT_FILL, 0.55), stat_tile(img, STAT_LOW, 0.2)]))
    else:
        rows.append(strip([tile(img, DISK), tile(img, NEAR_BLACK)]))
    if (kind, name) in SMALL or kind == "battery":
        s = shrink(img, 32)
        zoom = s.resize((96, 96), Image.Resampling.NEAREST)
        small = [tile(s, DISK), tile(s, NEAR_BLACK)]
        if kind != "battery":
            small.append(tile(s, TEAL))
        rows.append(strip(small + [tile(zoom, NEAR_BLACK)]))
    w = max(r.width for r in rows) + 2 * PAD
    h = 20 + sum(r.height for r in rows) + PAD * len(rows)
    out = Image.new("RGB", (w, h), PAPER)
    d = ImageDraw.Draw(out)
    d.text((PAD, 4), f"{kind}.{name}", fill=INK)
    y = 20
    for r in rows:
        out.paste(r, ((w - r.width) // 2, y))
        y += r.height + PAD
    return out


def review(results: dict[str, Image.Image], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for kind, spec in SHEETS.items():
        cards = [card(kind, name, results[f"{kind}/{name}"]) for name in spec["cells"]]
        cw = max(c.width for c in cards)
        ch = max(c.height for c in cards)
        cols = 4 if kind in ("menus", "icons") else min(4, len(cards))
        rows = (len(cards) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw + (cols + 1) * PAD, rows * ch + (rows + 1) * PAD), PAPER)
        for i, c in enumerate(cards):
            x = PAD + (i % cols) * (cw + PAD)
            y = PAD + (i // cols) * (ch + PAD)
            sheet.paste(c, (x, y))
        sheet.save(out_dir / f"ui-{kind}.png")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--sheets", type=Path, default=HERE, help="directory holding the sheets")
    ap.add_argument("--out", type=Path, default=SLICE / "hires", help="output root")
    ap.add_argument("--review", action="store_true", help="also write build/hires-review/ui-*.png")
    ap.add_argument("--review-dir", type=Path, default=REPO / "build" / "hires-review")
    args = ap.parse_args()
    results = extract(args.sheets, args.out)
    for key in sorted(results):
        path = args.out / f"{key}.png"
        print(hashlib.sha256(path.read_bytes()).hexdigest()[:16], f"{key}.png")
    if args.review:
        review(results, args.review_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
