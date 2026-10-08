#include "pet_draw.h"

static JelliRect joined(JelliRect a, JelliRect b)
{
    if (!a.width || !a.height)
        return b;
    if (!b.width || !b.height)
        return a;
    unsigned x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    unsigned right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    unsigned bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return (JelliRect){x, y, right - x, bottom - y};
}

static void draw_particle(JelliSurface *s, const JelliParticle *p)
{
    static const uint16_t colors[] = {0xff99u, 0xf62cu, 0xfc73u, 0x8736u};
    int radius = p->life < 6u ? 1 : 2 + (int)((p->style >> 2) & 1u);
    int x = p->x / 16, y = p->y / 16;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = x + dx, py = y + dy;
            if (px < 0 || py < 0 || px >= JELLI_WIDTH || py >= JELLI_HEIGHT)
                continue;
            int rx = 2 * px - 465, ry = 2 * py - 465;
            if (rx * rx + ry * ry > 466 * 466 || ((p->style & 8u) && dx && dy))
                continue;
            s->pixels[(unsigned)py * s->stride + (unsigned)px] = colors[p->style & 3u];
        }
    }
}

/* Component-wise integer alpha: no cross-channel carry, scratch surface or heap. */
static uint16_t blend(uint16_t back, uint16_t front, unsigned alpha)
{
    unsigned inverse = 16u - alpha;
    unsigned r = (((back >> 11) & 31u) * inverse + ((front >> 11) & 31u) * alpha) / 16u;
    unsigned g = (((back >> 5) & 63u) * inverse + ((front >> 5) & 63u) * alpha) / 16u;
    unsigned b = ((back & 31u) * inverse + (front & 31u) * alpha) / 16u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void draw_magic(JelliSurface *s, const JelliParticle *p)
{
    const JelliAsset *a = jelli_asset_find(9001u + (p->style & 7u));
    if (!a)
        return;
    unsigned scale = (p->style & 8u) ? 2u : 1u;
    int left = p->x / 16 - (int)(a->width * scale / 2u);
    int top = p->y / 16 - (int)(a->height * scale / 2u);
    unsigned alpha = p->life < 8u ? (unsigned)p->life * 2u : 16u;
    for (unsigned y = 0; y < a->height * scale; ++y) {
        for (unsigned x = 0; x < a->width * scale; ++x) {
            unsigned row = y / scale, col = x / scale;
            if (!(a->mask[row * a->mask_stride + col / 8u] & (1u << (7u - col % 8u))))
                continue;
            int px = left + (int)x, py = top + (int)y;
            if (px < 0 || py < 0 || px >= JELLI_WIDTH || py >= JELLI_HEIGHT)
                continue;
            int rx = 2 * px - 465, ry = 2 * py - 465;
            if (rx * rx + ry * ry > 466 * 466)
                continue;
            uint16_t *dest = &s->pixels[(unsigned)py * s->stride + (unsigned)px];
            *dest = blend(*dest, a->pixels[row * a->width + col], alpha);
        }
    }
}

static void sleep_layer(JelliSurface *s, const JelliParticle *p, int offset, uint16_t color)
{
    const uint8_t *rows = jelli_asset_glyph('Z');
    const unsigned scale = 2u;
    unsigned alpha = p->life < 8u ? (unsigned)p->life * 2u : 16u;
    for (unsigned y = 0; y < 12u * scale; ++y) {
        for (unsigned x = 0; x < 8u * scale; ++x) {
            if (!(rows[y / scale] & (1u << (7u - x / scale))))
                continue;
            int px = p->x / 16 + offset + (int)x - (int)(4u * scale);
            int py = p->y / 16 + offset + (int)y - (int)(6u * scale);
            if (px < 0 || py < 0 || px >= JELLI_WIDTH || py >= JELLI_HEIGHT)
                continue;
            int rx = 2 * px - 465, ry = 2 * py - 465;
            if (rx * rx + ry * ry > 466 * 466)
                continue;
            uint16_t *dest = &s->pixels[(unsigned)py * s->stride + (unsigned)px];
            *dest = blend(*dest, color, alpha);
        }
    }
}

static void draw_sleep(JelliSurface *s, const JelliParticle *p)
{
    sleep_layer(s, p, 2, 0x0841u);
    sleep_layer(s, p, 0, 0x64dfu);
}

void jelli_pet_draw_particles(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui)
{
    JelliParticles *p = &ui->particles;
    JelliRect current = jelli_particles_bounds(p);
    JelliRect region = joined(p->previous, current);
    if (!region.width || (!p->changed && !surface->damage.width))
        return;
    bool full = surface->damage.width == JELLI_WIDTH && surface->damage.height == JELLI_HEIGHT;
    if (!full)
        jelli_pet_draw_region(surface, game, ui, &ui->last_view, region);
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i)
        if (p->items[i].life) {
            if (p->items[i].style & 64u)
                draw_sleep(surface, &p->items[i]);
            else if (p->items[i].style & 128u)
                draw_magic(surface, &p->items[i]);
            else
                draw_particle(surface, &p->items[i]);
        }
    surface->damage = joined(surface->damage, region);
    p->previous = current;
    p->changed = false;
}
