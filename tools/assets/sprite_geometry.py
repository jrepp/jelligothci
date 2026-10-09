"""Build-time sprite measurements in LOGICAL Q8 pixel-edge coordinates.

Coverage is alpha >= 128 at the stored resolution; legacy art has binary alpha, so
this equals "any alpha" there. High-resolution art stores `density` (D) pixels per
logical pixel: measurements are taken on stored pixels and divided by D, so screen
layout uses the same logical units for every asset. The SDL pack decoder
(ports/sdl/asset_pack.c) repeats these exact integer formulas.
"""
GROUND_BAND_ROWS = 3
OPAQUE_ALPHA = 128


def covered_points(image):
    """Stored (x, y) of every pixel whose alpha is at least OPAQUE_ALPHA."""
    width = image.width
    alpha = image.getchannel("A").getdata()
    return [(i % width, i // width) for i, a in enumerate(alpha) if a >= OPAQUE_ALPHA]


def stored_bounds(image):
    """Exclusive stored coverage bounds [left, top, right, bottom], or None if empty."""
    points = covered_points(image)
    if not points:
        return None
    xs = [x for x, _ in points]
    ys = [y for _, y in points]
    return [min(xs), min(ys), max(xs) + 1, max(ys) + 1]


def logical_bounds(image, density=1):
    """Exclusive logical bounds: floor for left/top, ceiling for right/bottom."""
    bounds = stored_bounds(image)
    if bounds is None:
        raise ValueError("Cannot bound an empty sprite")
    left, top, right, bottom = bounds
    return [left // density, top // density, -(-right // density), -(-bottom // density)]


def _q8(center_sum, count, density):
    """Mean of (2x + 1) stored half-pixels as logical Q8, integer round-half-up."""
    # Do not use Python's ties-to-even round(); C uses the same integer expression.
    return (center_sum * 128 + count * density // 2) // (count * density)


def ground_anchor_q8(image, density=1):
    """Centroid of pixel centers in the lowest covered band, on the bottom edge.

    X averages every covered stored pixel in the bottom three LOGICAL rows (3 * D
    stored rows). Y is the exclusive bottom bound, keeping the lowest pixel on the
    same ground line. Empty sprites have no contact.
    """
    points = covered_points(image)
    if not points:
        raise ValueError("Cannot ground an empty sprite")
    bottom = max(y for _, y in points) + 1
    band = [2 * x + 1 for x, y in points if y >= bottom - GROUND_BAND_ROWS * density]
    return [_q8(sum(band), len(band), density), (bottom * 256 + density // 2) // density]


def opaque_centroid_q8(image, density=1):
    """Center of covered pixel centers, independent of transparent canvas padding."""
    points = covered_points(image)
    if not points:
        raise ValueError("Cannot center an empty sprite")
    return [_q8(sum(2 * p[axis] + 1 for p in points), len(points), density) for axis in (0, 1)]
