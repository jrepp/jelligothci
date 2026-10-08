"""Build-time contact anchors; coordinates are Q8 pixel-edge coordinates."""
GROUND_BAND_ROWS = 3


def ground_anchor_q8(image):
    """Centroid of pixel centers in the lowest opaque band, on the bottom edge.

    X averages all covered pixels in the bottom three source rows. Y is the
    exclusive bottom bound, keeping the lowest pixel on the same ground line.
    Binary coverage matches the runtime mask. Empty sprites have no contact.
    """
    alpha = image.getchannel("A")
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError("Cannot ground an empty sprite")
    bottom = bounds[3]
    centers = [2 * x + 1 for y in range(max(0, bottom - GROUND_BAND_ROWS), bottom)
               for x in range(image.width) if alpha.getpixel((x, y))]
    # Integer round-half-up; do not use Python's ties-to-even round().
    return [(sum(centers) * 128 + len(centers) // 2) // len(centers), bottom * 256]


def opaque_centroid_q8(image):
    """Center of opaque pixel centers, independent of transparent canvas padding."""
    points = [(x, y) for y in range(image.height) for x in range(image.width)
              if image.getpixel((x, y))[3]]
    if not points:
        raise ValueError("Cannot center an empty sprite")
    count = len(points)
    return [(sum(2 * p[axis] + 1 for p in points) * 128 + count // 2) // count
            for axis in (0, 1)]
