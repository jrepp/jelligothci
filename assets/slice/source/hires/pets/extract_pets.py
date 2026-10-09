# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Extract the high-resolution pet frames from the generated pose sheets.

From the repository root:

    ./scripts/uv run --python 3.12 assets/slice/source/hires/pets/extract_pets.py
    ./scripts/uv run --python 3.12 assets/slice/source/hires/pets/extract_pets.py \
        --review build/hires-review

Inputs are the verbatim Gemini pose sheets kept beside this script (each has a
`<sheet>.prompt.txt` recording the model, aspect ratio and exact prompt). The
output is 32 RGBA PNGs, `assets/slice/hires/creatures/<pet>-<pose>.png`, each
exactly 192x192 (32 logical pixels at density 6), 8-bit straight alpha.

The extraction is deterministic: integer arithmetic plus fixed Pillow 12.0.0
operations, no randomness and no clock, so repeated runs write byte-identical
files. `--check` re-extracts in memory and fails if any written PNG differs.

Pipeline, per sheet:

1. Key colour: the per-channel median of a BORDER-pixel frame around the sheet
   (the sheets are JPEGs, so the key is measured, not assumed).
2. Coarse alpha: Euclidean RGB distance d from the key; alpha is 0 for
   d <= KEY_T0, 255 for d >= KEY_T1 and a linear ramp between. KEY_T0 sits
   above the JPEG noise of the flat background (measured p99.9 <= 38 for these
   sheets); every subject colour, including the darkest plum and the teal sweat
   drop (~210), is more than KEY_T1 from its key, except pinks and reds on the
   magenta sheet (Mint's mouth, cheeks and berry sit ~150-200 from magenta).
   So coarse-soft regions that are enclosed by opaque pixels and contain no
   background pixel (d <= KEY_T0) are interior details and become opaque,
   untouched by the matte, unmix and despill steps.
3. Edge matte: the ramp alone overestimates alpha at edges (subject colours sit
   ~230-350 from the key), leaving a key-tinted rim. "Sure" pixels have coarse
   alpha 255 throughout a (2*EDGE_BAND+1)^2 square; every other keyed pixel
   takes the foreground colour F of its nearest sure pixel (8-connected
   breadth-first search seeded in raster order; F is the mean of the sure
   pixels in the seed's 5x5 neighbourhood, damping JPEG noise) and its alpha is
   re-estimated as the projection (C-K).(F-K) / |F-K|^2, clamped to 0..1.
   Unreached pixels keep the coarse alpha.
4. Unmix and despill: a soft pixel C is treated as a*F + (1 - a)*K and its
   colour is recovered as (C - (1 - a)K) / a. In the edge band the key's
   dominant channels may then exceed the others by at most SPILL_ALLOW (green
   key: G <= max(R, B) + allow; magenta key: min(R, B) <= G + allow, both
   lowered together). Interior colours (cheeks, berries, the unwell tint) are
   never touched.
5. Islands: 8-connected components of alpha > 0 smaller than MIN_ISLAND px
   are discarded (JPEG speckle); the rest are assigned to the 4x2 grid cell
   that contains their bounding-box centre.

Per pet:

6. One scale for all eight poses: the smaller of TARGET_HEIGHT / idle-a height
   and the largest scale at which every pose fits FIT_W x FIT_H.
7. Each pose is padded, resampled once with Lanczos (Pillow resamples RGBA in
   premultiplied form), and alpha < HAZE becomes fully transparent black.
8. Placement: the alpha > 0 bounding box is centred on x = 96, and the lowest
   row with alpha >= 128 (the engine's coverage threshold, see
   tools/assets/sprite_geometry.py) is row 179, so the ground edge is y = 180.
   Opaque pixels must stay within columns 24..168 and below row 5.

`idle-b` is derived from the idle-a cell (scaled by IDLE_B_SX x IDLE_B_SY about
the feet) rather than taken from the sheet: the generated idle-b cells differ in
small facial details, which would flicker in the two-frame idle bob.
"""
import argparse
import hashlib
import io
import statistics
import sys
from collections import deque
from fractions import Fraction
from math import isqrt
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]
OUT_DIR = REPO / "assets/slice/hires/creatures"

FRAME = 192
CENTRE_X = 96
GROUND_ROW = 179          # lowest row with alpha >= COVER_ALPHA
COVER_ALPHA = 128
MIN_X, MAX_X = 24, 168    # opaque columns allowed (battery indicator sits left)
MIN_TOP = 6               # top padding
FIT_W, FIT_H = 141, 170   # fit box for the scale; leaves room for resample fringe

BORDER = 10               # px frame used to measure the key colour
KEY_T0, KEY_T1 = 64, 176  # coarse alpha ramp on RGB distance from the key
EDGE_BAND = 3             # edge-matte and despill radius (sheet px)
SPILL_ALLOW = {"green": 6, "magenta": 8}  # key-channel excess allowed at edges
MIN_ISLAND = 64           # smallest kept component (sheet px)
HAZE = 8                  # alpha below this becomes 0 after resampling
PAD = 6                   # transparent padding before resampling (sheet px)
IDLE_B_SX = Fraction(41, 40)  # idle-b: 2.5 % wider ...
IDLE_B_SY = Fraction(24, 25)  # ... and 4 % shorter, anchored at the feet

POSES = ("idle-a", "idle-b", "eating", "happy", "asleep", "unwell", "curious", "content")

# Sheet cells are numbered 0..7 in reading order on a 4 x 2 grid. Each pose is
# (sheet file, cell, mirrored) or None for the derived idle-b. A mirrored sheet is
# flipped left-right before keying and its cells are counted after the flip.
PETS = {
    "baby": {
        "key": "magenta",
        "target_height": 100,
        "poses": {
            "idle-a": ("baby-sheet.jpg", 0, False),
            "idle-b": None,
            "eating": ("baby-sheet.jpg", 2, False),
            "happy": ("baby-sheet.jpg", 3, False),
            "asleep": ("baby-sheet.jpg", 4, False),
            "unwell": ("baby-sheet.jpg", 5, False),
            "curious": ("baby-sheet-curious.jpg", 1, False),
            "content": ("baby-sheet.jpg", 7, False),
        },
    },
    "grown": {
        "key": "green",
        "target_height": 148,
        "poses": {
            "idle-a": ("grown-sheet.jpg", 0, False),
            "idle-b": None,
            "eating": ("grown-sheet.jpg", 2, False),
            "happy": ("grown-sheet.jpg", 3, False),
            "asleep": ("grown-sheet.jpg", 4, False),
            "unwell": ("grown-sheet.jpg", 5, False),
            "curious": ("grown-sheet-curious.jpg", 1, True),
            "content": ("grown-sheet.jpg", 7, False),
        },
    },
    "ninja": {
        "key": "green",
        "target_height": 148,
        "poses": {
            "idle-a": ("ninja-sheet.jpg", 0, False),
            "idle-b": None,
            "eating": ("ninja-sheet.jpg", 2, False),
            "happy": ("ninja-sheet.jpg", 3, False),
            "asleep": ("ninja-sheet.jpg", 4, False),
            "unwell": ("ninja-sheet.jpg", 5, False),
            "curious": ("ninja-sheet-curious.jpg", 6, False),
            "content": ("ninja-sheet.jpg", 7, False),
        },
    },
    "unicorn": {
        "key": "green",
        "target_height": 150,
        "poses": {
            "idle-a": ("unicorn-sheet.jpg", 0, False),
            "idle-b": None,
            "eating": ("unicorn-sheet.jpg", 2, False),
            "happy": ("unicorn-sheet.jpg", 3, False),
            "asleep": ("unicorn-sheet.jpg", 4, False),
            "unwell": ("unicorn-sheet.jpg", 5, False),
            "curious": ("unicorn-sheet.jpg", 6, False),
            "content": ("unicorn-sheet.jpg", 7, False),
        },
    },
}


class Keyed:
    """A keyed sheet: straight RGBA plus the kept components per grid cell."""

    def __init__(self, image, cells, key):
        self.image = image      # RGBA, transparent outside every kept component
        self.cells = cells      # cell index -> [x0, y0, x1, y1) or None
        self.key = key


def measure_key(rgb):
    w, h = rgb.size
    px = rgb.load()
    frame = [px[x, y] for y in range(h) for x in range(w)
             if x < BORDER or y < BORDER or x >= w - BORDER or y >= h - BORDER]
    return tuple(int(statistics.median_low(p[i] for p in frame)) for i in range(3))


def key_sheet(path, kind, mirrored=False):
    """Steps 1-5: alpha, edge matte, unmix, despill, islands and cells."""
    rgb = Image.open(path).convert("RGB")
    if mirrored:
        rgb = rgb.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    w, h = rgb.size
    key = measure_key(rgb)
    src = rgb.tobytes()
    n = w * h
    alpha = bytearray(n)
    span = KEY_T1 - KEY_T0
    for i in range(n):
        d = isqrt(sum((src[3 * i + c] - key[c]) ** 2 for c in range(3)))
        if d > KEY_T0:
            alpha[i] = 255 if d >= KEY_T1 else ((d - KEY_T0) * 255 + span // 2) // span

    # Interior details: soft pixels not 4-connected through soft pixels to any
    # background pixel (alpha 0, which includes the sheet border) are opaque.
    reached = bytearray(n)
    queue = deque(i for i in range(n) if alpha[i] == 0)
    for i in queue:
        reached[i] = 1
    while queue:
        i = queue.popleft()
        y, x = divmod(i, w)
        for j in ((i - 1) if x else -1, (i + 1) if x + 1 < w else -1,
                  (i - w) if y else -1, (i + w) if y + 1 < h else -1):
            if j >= 0 and not reached[j] and alpha[j] < 255:
                reached[j] = 1
                queue.append(j)
    for i in range(n):
        if not reached[i] and alpha[i] < 255:
            alpha[i] = 255

    # Sure foreground: coarse alpha 255 throughout a (2*EDGE_BAND+1)^2 square.
    coarse = Image.frombytes("L", (w, h), bytes(alpha))
    sure = coarse.filter(ImageFilter.MinFilter(2 * EDGE_BAND + 1)).tobytes()
    neighbours = [(dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy]

    def unknown(i):
        return alpha[i] and sure[i] != 255

    # Seeds: sure pixels touching an unknown one, with a 5x5 mean of sure colour.
    fg = {}
    queue = deque()
    for i in range(n):
        if sure[i] != 255:
            continue
        y, x = divmod(i, w)
        if not any(0 <= x + dx < w and 0 <= y + dy < h and unknown((y + dy) * w + x + dx)
                   for dx, dy in neighbours):
            continue
        acc, count = [0, 0, 0], 0
        for yy in range(max(0, y - 2), min(h, y + 3)):
            for xx in range(max(0, x - 2), min(w, x + 3)):
                j = yy * w + xx
                if sure[j] == 255:
                    acc = [acc[c] + src[3 * j + c] for c in range(3)]
                    count += 1
        fg[i] = tuple((acc[c] + count // 2) // count for c in range(3))
        queue.append(i)
    while queue:
        i = queue.popleft()
        y, x = divmod(i, w)
        for dx, dy in neighbours:
            if 0 <= x + dx < w and 0 <= y + dy < h:
                j = (y + dy) * w + x + dx
                if unknown(j) and j not in fg:
                    fg[j] = fg[i]
                    queue.append(j)

    out = bytearray(src)
    allow = SPILL_ALLOW[kind]
    for i in range(n):
        if not unknown(i):
            continue
        a = alpha[i]
        if i in fg:
            fk = [fg[i][c] - key[c] for c in range(3)]
            den = sum(v * v for v in fk)
            if den:
                num = sum(fk[c] * (src[3 * i + c] - key[c]) for c in range(3))
                a = max(0, min(255, (num * 255 + den // 2) // den))
        alpha[i] = a
        if a == 0:
            continue
        for c in range(3):
            v = src[3 * i + c] * 255 - (255 - a) * key[c]
            out[3 * i + c] = 0 if v <= 0 else min(255, (v + a // 2) // a)
        r, g, b = out[3 * i], out[3 * i + 1], out[3 * i + 2]
        if kind == "green":
            if g > max(r, b) + allow:
                out[3 * i + 1] = max(r, b) + allow
        else:
            excess = min(r, b) - (g + allow)
            if excess > 0:
                out[3 * i] = r - excess
                out[3 * i + 2] = b - excess

    # Islands: 8-connected components of alpha > 0, scanned in raster order.
    label = [0] * n
    comps = []
    for start in range(n):
        if alpha[start] == 0 or label[start]:
            continue
        lab = len(comps) + 1
        label[start] = lab
        queue = deque([start])
        members = []
        while queue:
            p = queue.popleft()
            members.append(p)
            y, x = divmod(p, w)
            for dy in (-1, 0, 1):
                yy = y + dy
                if yy < 0 or yy >= h:
                    continue
                for dx in (-1, 0, 1):
                    xx = x + dx
                    if xx < 0 or xx >= w:
                        continue
                    q = yy * w + xx
                    if alpha[q] and not label[q]:
                        label[q] = lab
                        queue.append(q)
        comps.append(members)

    cells = [None] * 8
    rgba = bytearray(4 * n)
    for members in comps:
        if len(members) < MIN_ISLAND:
            continue
        xs = [p % w for p in members]
        ys = [p // w for p in members]
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
        cell = ((y0 + y1) // 2 * 2 // h) * 4 + (x0 + x1) // 2 * 4 // w
        box = cells[cell]
        cells[cell] = [x0, y0, x1, y1] if box is None else [
            min(box[0], x0), min(box[1], y0), max(box[2], x1), max(box[3], y1)]
        for p in members:
            rgba[4 * p:4 * p + 3] = out[3 * p:3 * p + 3]
            rgba[4 * p + 3] = alpha[p]
    return Keyed(Image.frombytes("RGBA", (w, h), bytes(rgba)), cells, key)


def cell_crop(keyed, cell, source):
    box = keyed.cells[cell]
    if box is None:
        raise ValueError(f"{source}: cell {cell} is empty")
    x0, y0, x1, y1 = box
    w, h = keyed.image.size
    if x0 == 0 or y0 == 0 or x1 == w or y1 == h:
        raise ValueError(f"{source}: cell {cell} touches the sheet edge and may be cut off")
    crop = Image.new("RGBA", (x1 - x0 + 2 * PAD, y1 - y0 + 2 * PAD), (0, 0, 0, 0))
    crop.paste(keyed.image.crop(box), (PAD, PAD))
    return crop


def resample(crop, sx, sy):
    """Resize once by exact rational factors; Pillow premultiplies RGBA."""
    tw = (crop.width * sx.numerator * 2 + sx.denominator) // (2 * sx.denominator)
    th = (crop.height * sy.numerator * 2 + sy.denominator) // (2 * sy.denominator)
    im = crop.resize((tw, th), Image.Resampling.LANCZOS)
    px = bytearray(im.tobytes())
    for i in range(3, len(px), 4):
        if px[i] < HAZE:
            px[i - 3:i + 1] = b"\0\0\0\0"
    return Image.frombytes("RGBA", im.size, bytes(px))


def alpha_bounds(im, threshold):
    """Inclusive (x0, y0, x1, y1) of pixels with alpha >= threshold."""
    a = im.getchannel("A").tobytes()
    w = im.width
    xs = [i % w for i, v in enumerate(a) if v >= threshold]
    ys = [i // w for i, v in enumerate(a) if v >= threshold]
    if not xs:
        raise ValueError("empty sprite")
    return min(xs), min(ys), max(xs), max(ys)


def place(sprite, name):
    """Centre the alpha > 0 box on CENTRE_X and stand the coverage on GROUND_ROW."""
    x0, _, x1, _ = alpha_bounds(sprite, 1)
    _, _, _, cover_bottom = alpha_bounds(sprite, COVER_ALPHA)
    ox = CENTRE_X - (x0 + x1 + 1) // 2
    oy = GROUND_ROW - cover_bottom
    frame = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    frame.paste(sprite, (ox, oy))
    # Pixels pasted outside the canvas would be silently lost: refuse instead.
    sx0, sy0, sx1, sy1 = alpha_bounds(sprite, 1)
    if ox + sx0 < MIN_X or ox + sx1 > MAX_X or oy + sy0 < MIN_TOP or oy + sy1 >= FRAME:
        raise ValueError(f"{name}: placed bounds x {ox + sx0}..{ox + sx1} "
                         f"y {oy + sy0}..{oy + sy1} break the frame limits")
    return frame


def extract_pet(pet, spec, sheets):
    crops = {}
    for pose in POSES:
        source = spec["poses"][pose]
        if source is None:
            continue
        sheet, cell, mirrored = source
        crops[pose] = cell_crop(sheets[sheet, mirrored], cell, f"{pet}/{sheet}")
    # Content extents (without padding) decide the one scale for this pet.
    dims = {pose: (c.width - 2 * PAD, c.height - 2 * PAD) for pose, c in crops.items()}
    iw, ih = dims["idle-a"]
    dims["idle-b"] = (iw * IDLE_B_SX, ih * IDLE_B_SY)
    scale = Fraction(spec["target_height"], ih)
    for w, h in dims.values():
        scale = min(scale, Fraction(FIT_W) / Fraction(w), Fraction(FIT_H) / Fraction(h))
    frames = {}
    for pose in POSES:
        if pose == "idle-b":
            sprite = resample(crops["idle-a"], scale * IDLE_B_SX, scale * IDLE_B_SY)
        else:
            sprite = resample(crops[pose], scale, scale)
        frames[pose] = place(sprite, f"{pet}-{pose}")
    return frames, scale


def png_bytes(im):
    buf = io.BytesIO()
    im.save(buf, format="PNG", compress_level=9)
    return buf.getvalue()


def extract_all():
    sheets = {}
    for spec in PETS.values():
        for source in spec["poses"].values():
            if source and (source[0], source[2]) not in sheets:
                sheets[source[0], source[2]] = key_sheet(HERE / source[0], spec["key"], source[2])
    results = {}
    for pet, spec in PETS.items():
        frames, scale = extract_pet(pet, spec, sheets)
        results[pet] = (frames, scale)
    return results, sheets


def bounds_report(frames):
    lines = []
    for pose, im in frames.items():
        x0, y0, x1, y1 = alpha_bounds(im, COVER_ALPHA)
        a0, b0, a1, b1 = alpha_bounds(im, 1)
        lines.append(f"  {pose:8s} alpha>=128 x {x0:3d}..{x1:3d} y {y0:3d}..{y1:3d}"
                     f" | alpha>0 x {a0:3d}..{a1:3d} y {b0:3d}..{b1:3d}")
    return lines


# ---------------------------------------------------------------- review sheets

def ground_anchor(im):
    """Engine placement (core/pet_actor.c, sprite_geometry.py) at density 6."""
    a = im.getchannel("A").tobytes()
    w = im.width
    pts = [(i % w, i // w) for i, v in enumerate(a) if v >= COVER_ALPHA]
    bottom = max(y for _, y in pts) + 1
    band = [2 * x + 1 for x, y in pts if y >= bottom - 3 * 6]
    gx = (sum(band) * 128 + len(band) * 3) // (len(band) * 6)
    gy = (bottom * 256 + 3) // 6
    return 233 - (gx * 6 + 128) // 256, 256 - (gy * 6 + 128) // 256


def home_mock(base_path):
    """466x466 home screen without the old pixel pet, or a plain round panel."""
    size = 466
    if base_path and base_path.exists():
        base = Image.open(base_path).convert("RGB")
        bg = Image.open(REPO / "assets/slice/backgrounds/home.png").convert("RGB")
        bp, px = bg.load(), base.load()
        # The screen dims the background: base ~= 0.51 * bg - 4 away from the UI.
        old = Image.open(REPO / "assets/slice/creatures/ninja-idle-a.png").convert("RGBA")
        oa = old.getchannel("A").resize((192, 192), Image.Resampling.NEAREST)
        for y in range(192):
            for x in range(192):
                sx, sy = 137 + x, 76 + y
                v = bp[sx * bg.width // size, sy * bg.height // size]
                est = tuple(max(0, c * 51 // 100 - 4) for c in v)
                if oa.getpixel((x, y)) or any(abs(px[sx, sy][i] - est[i]) > 24 for i in range(3)):
                    if sx >= 140 and 70 <= sy <= 275:
                        px[sx, sy] = est
        mock = base.convert("RGBA")
    else:
        mock = Image.new("RGBA", (size, size), (16, 16, 16, 255))
        ImageDraw.Draw(mock).ellipse((0, 0, size - 1, size - 1), fill=(0, 0, 0, 255))
    return mock


def review(results, out_dir, base_path):
    out_dir.mkdir(parents=True, exist_ok=True)
    mock = home_mock(base_path)
    for pet, (frames, scale) in results.items():
        gap, mw = 8, 466
        width = 4 * (mw + gap) + gap
        height = gap + FRAME + gap + 64 + gap + 2 * (mw + gap)
        sheet = Image.new("RGBA", (width, height), (16, 16, 16, 255))
        draw = ImageDraw.Draw(sheet)
        for i, pose in enumerate(POSES):
            im = frames[pose]
            x = gap + i * (FRAME + gap // 2)
            draw.rectangle((x, gap, x + FRAME - 1, gap + FRAME - 1), outline=(40, 40, 40, 255))
            draw.line((x + MIN_X, gap, x + MIN_X, gap + FRAME - 1), fill=(60, 30, 30, 255))
            draw.line((x + MAX_X, gap, x + MAX_X, gap + FRAME - 1), fill=(60, 30, 30, 255))
            sheet.alpha_composite(im, (x, gap))
            small = im.resize((64, 64), Image.Resampling.LANCZOS)
            sheet.alpha_composite(small, (x + 64, 2 * gap + FRAME))
            m = mock.copy()
            ax, ay = ground_anchor(im)
            m.alpha_composite(im, (ax, ay))
            mx = gap + (i % 4) * (mw + gap)
            my = 3 * gap + FRAME + 64 + (i // 4) * (mw + gap)
            sheet.alpha_composite(m, (mx, my))
        sheet.convert("RGB").save(out_dir / f"pets-{pet}.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed PNGs differ from a fresh extraction")
    parser.add_argument("--review", type=Path, help="also write review sheets to this directory")
    parser.add_argument("--base", type=Path, default=REPO / "build/hires-review/base.bmp",
                        help="466x466 home-screen snapshot for the review mock")
    args = parser.parse_args()
    results, sheets = extract_all()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    stale = []
    for pet, (frames, scale) in results.items():
        keys = sorted({str(s.key) for (name, _), s in sheets.items() if name.startswith(pet)})
        print(f"{pet}: scale {scale} ({float(scale):.4f}), measured key {', '.join(keys)}")
        for line in bounds_report(frames):
            print(line)
        for pose, im in frames.items():
            data = png_bytes(im)
            path = OUT_DIR / f"{pet}-{pose}.png"
            digest.update(path.name.encode() + data)
            if args.check:
                if not path.exists() or path.read_bytes() != data:
                    stale.append(path.name)
            else:
                path.write_bytes(data)
    print(f"sha256 of all frames: {digest.hexdigest()}")
    if args.review:
        review(results, args.review, args.base)
    if stale:
        print("stale: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
