#include "jelli/particles.h"

_Static_assert(sizeof(JelliParticle) == 8u, "Particle packing changed");
_Static_assert(sizeof(JelliParticles) <= 224u, "Particle pool exceeds 224 bytes");

static uint32_t random_bits(JelliParticles *p)
{
    uint32_t value = p->random_state ? p->random_state : UINT32_C(0x64a72839);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    p->random_state = value;
    return value;
}

void jelli_particles_burst_tuned(JelliParticles *p, int x, int y, bool accepted, unsigned amount,
                                 unsigned spread)
{
    if (!p || x < 0 || y < 0 || x >= JELLI_WIDTH || y >= JELLI_HEIGHT ||
        amount > JELLI_PARTICLE_CAPACITY || spread < 25u || spread > 150u)
        return;
    unsigned count = accepted || amount < 3u ? amount : 3u;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t bits = random_bits(p);
        JelliParticle *item = &p->items[p->next];
        *item = (JelliParticle){
            .x = (int16_t)(x * 16),
            .y = (int16_t)(y * 16),
            .vx = (int8_t)(((int)(bits % 97u) - 48) * (int)spread / 100),
            .vy = (int8_t)((-20 - (int)((bits >> 8) % 53u)) * (int)spread / 100),
            .life = (uint8_t)(accepted ? 22u + i % 8u : 12u),
            .style = (uint8_t)(accepted ? 128u | ((bits >> 16) % 8u) | ((i & 1u) << 3)
                                        : (bits >> 16) & 15u)};
        p->next = (uint8_t)((p->next + 1u) % JELLI_PARTICLE_CAPACITY);
    }
    p->changed = true;
}

static void tick(JelliParticles *p)
{
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i) {
        JelliParticle *item = &p->items[i];
        if (!item->life)
            continue;
        --item->life;
        item->x = (int16_t)((int)item->x + item->vx);
        item->y = (int16_t)((int)item->y + item->vy);
        if (!(item->style & 64u) && item->vy < 120)
            item->vy = (int8_t)(item->vy + 3);
        p->changed = true;
    }
}

void jelli_particles_advance_scaled(JelliParticles *p, uint64_t elapsed_ms, unsigned step_ms)
{
    if (!p || step_ms < 5u || step_ms > 200u)
        return;
    if (elapsed_ms >= UINT64_C(40) * step_ms) {
        for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i) {
            if (p->items[i].life)
                p->changed = true;
            p->items[i].life = 0;
        }
        p->remainder_ms = 0;
        return;
    }
    unsigned admitted = (unsigned)elapsed_ms + p->remainder_ms % step_ms;
    unsigned ticks = admitted / step_ms; /* At most 40 steps; never unbounded catch-up. */
    p->remainder_ms = (uint8_t)(admitted % step_ms);
    for (unsigned i = 0; i < ticks; ++i)
        tick(p);
}

unsigned jelli_particles_count(const JelliParticles *p)
{
    unsigned count = 0;
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i)
        if (p->items[i].life)
            ++count;
    return count;
}

JelliRect jelli_particles_bounds(const JelliParticles *p)
{
    int left = JELLI_WIDTH, top = JELLI_HEIGHT, right = 0, bottom = 0;
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i) {
        const JelliParticle *item = &p->items[i];
        if (!item->life)
            continue;
        int x = item->x / 16, y = item->y / 16;
        int radius = (item->style & 192u) ? 16 : 3;
        if (x + radius < 0 || x - radius >= JELLI_WIDTH || y + radius < 0 ||
            y - radius >= JELLI_HEIGHT)
            continue;
        if (x - radius < left)
            left = x - radius;
        if (y - radius < top)
            top = y - radius;
        if (x + radius + 1 > right)
            right = x + radius + 1;
        if (y + radius + 1 > bottom)
            bottom = y + radius + 1;
    }
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > JELLI_WIDTH)
        right = JELLI_WIDTH;
    if (bottom > JELLI_HEIGHT)
        bottom = JELLI_HEIGHT;
    if (left >= right || top >= bottom)
        return (JelliRect){0};
    return (JelliRect){(unsigned)left, (unsigned)top, (unsigned)(right - left),
                       (unsigned)(bottom - top)};
}

void jelli_particles_burst(JelliParticles *p, int x, int y, bool accepted)
{
    jelli_particles_burst_tuned(p, x, y, accepted, 8u, 100u);
}
void jelli_particles_advance(JelliParticles *p, uint64_t elapsed_ms)
{
    jelli_particles_advance_scaled(p, elapsed_ms, 20u);
}
