#include "jelli/agent_sprite.h"
#include "jelli/agent_scene.h"
#include <math.h>
#include <string.h>

enum { SHIFT = 16, ONE = 1 << SHIFT };

static unsigned stored_width(const JelliAsset *asset)
{
    return (unsigned)asset->width * jelli_asset_density(asset);
}

static unsigned stored_height(const JelliAsset *asset)
{
    return (unsigned)asset->height * jelli_asset_density(asset);
}

static bool readable(const JelliAsset *asset)
{
    return asset && asset->pixels && jelli_asset_density(asset) && stored_width(asset) &&
           stored_height(asset) && (asset->alpha || asset->mask);
}

JelliRect jelli_agent_asset_content(const JelliAsset *asset)
{
    JelliRect none = {0u, 0u, 0u, 0u};
    if (!readable(asset))
        return none;
    unsigned left = UINT32_MAX, top = UINT32_MAX, right = 0u, bottom = 0u;
    for (unsigned y = 0u; y < stored_height(asset); ++y) {
        for (unsigned x = 0u; x < stored_width(asset); ++x) {
            if (!jelli_asset_alpha_at(asset, x, y))
                continue;
            left = x < left ? x : left;
            top = y < top ? y : top;
            right = x + 1u > right ? x + 1u : right;
            bottom = y + 1u > bottom ? y + 1u : bottom;
        }
    }
    return right ? (JelliRect){left, top, right - left, bottom - top} : none;
}

float jelli_agent_asset_reach(const JelliAsset *asset, JelliRect area)
{
    if (!readable(asset))
        return 0.0f;
    float cx = (float)area.x + (float)area.width / 2.0f,
          cy = (float)area.y + (float)area.height / 2.0f;
    float far = 0.0f;
    for (unsigned y = 0u; y < stored_height(asset); ++y) {
        for (unsigned x = 0u; x < stored_width(asset); ++x) {
            if (jelli_asset_alpha_at(asset, x, y) < JELLI_ASSET_OPAQUE_ALPHA)
                continue;
            /* The far corner of the texel, so the whole texel is inside. */
            float dx = fabsf((float)x + 0.5f - cx) + 0.5f, dy = fabsf((float)y + 0.5f - cy) + 0.5f;
            float d = dx * dx + dy * dy;
            far = d > far ? d : far;
        }
    }
    return sqrtf(far);
}

/* One stored texel, premultiplied: colour channels times alpha, then alpha. */
typedef struct {
    uint32_t r, g, b, a;
} Texel;

static Texel texel(const JelliAsset *asset, unsigned x, unsigned y)
{
    uint32_t a = jelli_asset_alpha_at(asset, x, y);
    uint16_t c = a ? asset->pixels[(size_t)y * stored_width(asset) + x] : 0u;
    return (Texel){((c >> 11) & 31u) * a, ((c >> 5) & 63u) * a, (c & 31u) * a, a};
}

/* Bilinear sample at a stored position in Q16; taps outside the image are clear. */
static uint8_t sample(const JelliAsset *asset, int64_t sx, int64_t sy, uint16_t *color)
{
    int64_t x0 = sx >> SHIFT, y0 = sy >> SHIFT;
    uint32_t fx = (uint32_t)((sx & (ONE - 1)) >> 8), fy = (uint32_t)((sy & (ONE - 1)) >> 8);
    const uint32_t weights[4] = {(256u - fx) * (256u - fy), fx * (256u - fy), (256u - fx) * fy,
                                 fx * fy};
    uint32_t r = 0u, g = 0u, b = 0u, a = 0u;
    for (unsigned i = 0u; i < 4u; ++i) {
        int64_t x = x0 + (int64_t)(i & 1u), y = y0 + (int64_t)(i >> 1u);
        if (x < 0 || y < 0)
            continue; /* alpha_at already clears taps past the far edges. */
        Texel t = texel(asset, (unsigned)x, (unsigned)y);
        r += (t.r * weights[i] + 128u) >> 8;
        g += (t.g * weights[i] + 128u) >> 8;
        b += (t.b * weights[i] + 128u) >> 8;
        a += t.a * weights[i];
    }
    /* a is alpha x 65536; colour sums are channel x alpha x 256. */
    uint8_t alpha = (uint8_t)((a + 32768u) >> 16);
    if (!alpha)
        return 0u;
    uint32_t total = (a + 128u) >> 8;
    *color = (uint16_t)((((r + total / 2u) / total) << 11) | (((g + total / 2u) / total) << 5) |
                        ((b + total / 2u) / total));
    return alpha;
}

bool jelli_agent_sprite_resample(JelliAgentSprite *sprite, const JelliAsset *asset,
                                 JelliRect source)
{
    if (!sprite || !sprite->pixels || !sprite->alpha || !sprite->width || !sprite->height ||
        !readable(asset) || !source.width || !source.height ||
        (uint64_t)source.x + source.width > stored_width(asset) ||
        (uint64_t)source.y + source.height > stored_height(asset))
        return false;
    /* Output pixel centres map onto stored pixel centres across the source area. */
    int64_t step_x = ((int64_t)source.width << SHIFT) / sprite->width;
    int64_t step_y = ((int64_t)source.height << SHIFT) / sprite->height;
    int64_t start_x = ((int64_t)source.x << SHIFT) + step_x / 2 - ONE / 2;
    int64_t start_y = ((int64_t)source.y << SHIFT) + step_y / 2 - ONE / 2;
    for (unsigned row = 0u; row < sprite->height; ++row) {
        for (unsigned column = 0u; column < sprite->width; ++column) {
            size_t at = (size_t)row * sprite->width + column;
            uint16_t color = 0u;
            sprite->alpha[at] =
                sample(asset, start_x + step_x * column, start_y + step_y * row, &color);
            sprite->pixels[at] = color;
        }
    }
    return true;
}

JelliRect jelli_agent_sprite_area(const JelliSurface *surface, const JelliAgentSprite *sprite,
                                  int x, int y)
{
    JelliRect area = {0u, 0u, 0u, 0u};
    int64_t x0 = x > 0 ? x : 0, y0 = y > 0 ? y : 0;
    int64_t x1 = (int64_t)x + sprite->width, y1 = (int64_t)y + sprite->height;
    x1 = x1 < surface->width ? x1 : surface->width;
    y1 = y1 < surface->height ? y1 : surface->height;
    if (x1 > x0 && y1 > y0)
        area = (JelliRect){(unsigned)x0, (unsigned)y0, (unsigned)(x1 - x0), (unsigned)(y1 - y0)};
    return area;
}

static uint16_t blend(uint16_t back, uint16_t front, unsigned alpha)
{
    if (alpha >= 255u)
        return front;
    unsigned inverse = 255u - alpha;
    unsigned r = (((back >> 11) & 31u) * inverse + ((front >> 11) & 31u) * alpha + 127u) / 255u;
    unsigned g = (((back >> 5) & 63u) * inverse + ((front >> 5) & 63u) * alpha + 127u) / 255u;
    unsigned b = ((back & 31u) * inverse + (front & 31u) * alpha + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void jelli_agent_sprite_draw(const JelliSurface *surface, const JelliAgentSprite *sprite, int x,
                             int y, bool mirror, JelliRect clip)
{
    JelliRect all = {0u, 0u, surface->width, surface->height};
    clip = jelli_agent_rect_intersect(clip, all);
    for (unsigned row = clip.y; row < clip.y + clip.height; ++row)
        memset(surface->pixels + (size_t)row * surface->stride + clip.x, 0,
               clip.width * sizeof(uint16_t));
    JelliRect area =
        jelli_agent_rect_intersect(clip, jelli_agent_sprite_area(surface, sprite, x, y));
    for (unsigned row = area.y; row < area.y + area.height; ++row) {
        uint16_t *line = surface->pixels + (size_t)row * surface->stride;
        size_t base = (size_t)(row - (int64_t)y) * sprite->width;
        for (unsigned column = area.x; column < area.x + area.width; ++column) {
            unsigned offset = (unsigned)((int64_t)column - x);
            size_t at = base + (mirror ? sprite->width - 1u - offset : offset);
            if (sprite->alpha[at])
                line[column] = blend(line[column], sprite->pixels[at], sprite->alpha[at]);
        }
    }
}
