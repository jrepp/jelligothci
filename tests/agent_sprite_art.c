#include "agent_sprite_art.h"

uint32_t art_next(uint32_t *state)
{
    uint32_t x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

unsigned art_below(uint32_t *state, unsigned bound) { return bound ? art_next(state) % bound : 0u; }

void *art_calloc(size_t count, size_t size)
{
    /* Exactly count elements, so the sanitizer sees one past the end; never empty. */
    void *memory = count ? calloc(count, size) : NULL;
    if (!memory) {
        fputs("empty plane or out of memory\n", stderr);
        exit(1);
    }
    return memory;
}

static uint8_t coverage(uint32_t *state, ArtCoverage kind)
{
    switch (kind) {
    case ART_OPAQUE:
        return 255u;
    case ART_NOISE:
        return (uint8_t)art_below(state, 256u);
    case ART_BINARY:
        return art_below(state, 2u) ? 255u : 0u;
    case ART_EMPTY:
        return 0u;
    case ART_SPARSE:
    case ART_KINDS:
    default:
        return art_below(state, 3u) ? 0u : (uint8_t)(1u + art_below(state, 255u));
    }
}

void art_set_alpha(Art *art, unsigned x, unsigned y, uint8_t alpha)
{
    size_t stride = (art->width + 7u) / 8u, at = (size_t)y * stride + x / 8u;
    if (art->alpha)
        art->alpha[(size_t)y * art->width + x] = alpha;
    else if (alpha)
        art->mask[at] |= (uint8_t)(0x80u >> (x % 8u));
    else
        art->mask[at] &= (uint8_t)~(0x80u >> (x % 8u));
}

Art art_make(uint32_t *state, unsigned width, unsigned height, unsigned density, bool mask,
             ArtCoverage kind)
{
    unsigned d = density ? density : 1u;
    Art art = {.width = width * d, .height = height * d};
    size_t count = (size_t)art.width * art.height, stride = (art.width + 7u) / 8u;
    art.pixels = art_calloc(count, sizeof(uint16_t));
    art.alpha = mask ? NULL : art_calloc(count, 1u);
    art.mask = mask ? art_calloc(stride * art.height, 1u) : NULL;
    for (unsigned y = 0u; y < art.height; ++y)
        for (unsigned x = 0u; x < art.width; ++x) {
            art.pixels[(size_t)y * art.width + x] = (uint16_t)art_next(state);
            art_set_alpha(&art, x, y,
                          coverage(state, mask && kind == ART_NOISE ? ART_BINARY : kind));
        }
    art.asset = (JelliAsset){.id = 1u,
                             .width = (uint16_t)width,
                             .height = (uint16_t)height,
                             .pixels = art.pixels,
                             .mask = art.mask,
                             .mask_stride = (uint8_t)stride,
                             .density = (uint8_t)density,
                             .alpha = art.alpha};
    return art;
}

Art art_random(uint32_t *state)
{
    static const unsigned densities[] = {0u, 1u, 2u, 3u, 8u};
    unsigned density = densities[art_below(state, 5u)];
    bool mask = density <= 1u && art_below(state, 3u) == 0u;
    unsigned limit = density >= 8u ? 4u : 24u;
    return art_make(state, 1u + art_below(state, limit), 1u + art_below(state, limit), density,
                    mask, (ArtCoverage)art_below(state, ART_KINDS));
}

void art_free(Art *art)
{
    free(art->pixels);
    free(art->alpha);
    free(art->mask);
    *art = (Art){0};
}

uint8_t art_alpha(const Art *art, long x, long y)
{
    if (x < 0 || y < 0 || x >= (long)art->width || y >= (long)art->height)
        return 0u;
    if (art->alpha)
        return art->alpha[(size_t)y * art->width + (size_t)x];
    size_t stride = (art->width + 7u) / 8u;
    return (art->mask[(size_t)y * stride + (size_t)x / 8u] & (0x80u >> (x % 8))) ? 255u : 0u;
}

JelliRect art_source(uint32_t *state, const Art *art)
{
    unsigned w = 1u + art_below(state, art->width), h = 1u + art_below(state, art->height);
    return (JelliRect){art_below(state, art->width - w + 1u),
                       art_below(state, art->height - h + 1u), w, h};
}

unsigned art_size(uint32_t *state, unsigned source)
{
    switch (art_below(state, 4u)) {
    case 0:
        return 1u;
    case 1:
        return 1u + art_below(state, 3u * source + 2u);
    case 2:
        return source * (1u + art_below(state, 12u));
    default:
        return 1u + art_below(state, 160u);
    }
}

Planes planes_make(unsigned width, unsigned height)
{
    size_t count = (size_t)width * height;
    Planes p = {art_calloc(count, sizeof(uint16_t)), art_calloc(count, 1u), {0}};
    p.sprite = (JelliAgentSprite){p.pixels, p.alpha, width, height};
    return p;
}

void planes_free(Planes *planes)
{
    free(planes->pixels);
    free(planes->alpha);
    *planes = (Planes){0};
}

size_t planes_count(const Planes *planes)
{
    return (size_t)planes->sprite.width * planes->sprite.height;
}
