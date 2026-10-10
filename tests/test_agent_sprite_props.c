/* Property, metamorphic and reference tests for jelli_agent_sprite_resample
 * (core/agent/sprite.c): random assets (densities 1 to 8, alpha planes and legacy
 * masks, garbage colour under every clear texel), sources at the image edges, and
 * output sizes from 1x1 to huge stretch and shrink ratios. Each case prints its seed. */
#include "agent_sprite_art.h"
#include <math.h>
#include <string.h>

enum { SEEDS = 400u };

/* Output pixel centres on stored pixel centres, premultiplied bilinear in doubles,
 * taps outside the stored image clear. Returns alpha; rgb gets straight colour. */
static double reference(const Art *art, JelliRect source, const JelliAgentSprite *s, unsigned col,
                        unsigned row, double rgb[3])
{
    double sx = source.x + (col + 0.5) * source.width / s->width - 0.5;
    double sy = source.y + (row + 0.5) * source.height / s->height - 0.5;
    double fx = sx - floor(sx), fy = sy - floor(sy), total = 0.0;
    long x0 = (long)floor(sx), y0 = (long)floor(sy);
    rgb[0] = rgb[1] = rgb[2] = 0.0;
    for (unsigned i = 0u; i < 4u; ++i) {
        long x = x0 + (long)(i & 1u), y = y0 + (long)(i >> 1u);
        double a = art_alpha(art, x, y) * ((i & 1u) ? fx : 1.0 - fx) * ((i >> 1u) ? fy : 1.0 - fy);
        if (a <= 0.0)
            continue;
        uint16_t c = art->pixels[(size_t)y * art->width + (size_t)x];
        rgb[0] += a * ((c >> 11) & 31u);
        rgb[1] += a * ((c >> 5) & 63u);
        rgb[2] += a * (c & 31u);
        total += a;
    }
    for (unsigned k = 0u; total > 0.0 && k < 3u; ++k)
        rgb[k] /= total;
    return total;
}

/* The worst differences from the reference in the run, printed at the end. */
static double worst_alpha, worst_channel;

static void compare_with_reference(const Art *art, JelliRect source, const Planes *p, uint32_t seed)
{
    for (unsigned row = 0u; row < p->sprite.height; ++row)
        for (unsigned col = 0u; col < p->sprite.width; ++col) {
            size_t at = (size_t)row * p->sprite.width + col;
            double rgb[3], alpha = reference(art, source, &p->sprite, col, row, rgb);
            double da = fabs(alpha - p->alpha[at]);
            worst_alpha = da > worst_alpha ? da : worst_alpha;
            /* Weights are cut to 1/256 of a texel per axis, and the Q16 step drifts by
             * under 1/65536 of a texel per pixel: alpha moves at most 255 per texel. */
            double bound = 0.5 + 255.0 * (2.0 / 256.0 + (col + row + 2.0) / 65536.0);
            CHECK_CASE(da <= bound, seed);
            CHECK_CASE(p->alpha[at] || p->pixels[at] == 0u, seed);
            if (p->alpha[at] < 64u || alpha < 64.0)
                continue; /* faint pixels carry little colour precision */
            uint16_t c = p->pixels[at];
            const double got[3] = {(c >> 11) & 31u, (c >> 5) & 63u, c & 31u};
            for (unsigned k = 0u; k < 3u; ++k) {
                double dc = fabs(got[k] - rgb[k]);
                worst_channel = dc > worst_channel ? dc : worst_channel;
                CHECK_CASE(dc <= 2.0, seed);
            }
        }
}

/* Against the reference; clear output is colourless; the same call is deterministic. */
static void resample_matches_the_reference(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 2654435761u;
        Art art = art_random(&state);
        JelliRect source = art_source(&state, &art);
        Planes p = planes_make(art_size(&state, source.width), art_size(&state, source.height));
        CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset, source), seed);
        compare_with_reference(&art, source, &p, seed);
        Planes again = planes_make(p.sprite.width, p.sprite.height);
        memset(again.pixels, 0xa5, planes_count(&again) * sizeof(uint16_t));
        memset(again.alpha, 0xa5, planes_count(&again));
        CHECK_CASE(jelli_agent_sprite_resample(&again.sprite, &art.asset, source), seed);
        CHECK_CASE(!memcmp(p.pixels, again.pixels, planes_count(&p) * sizeof(uint16_t)), seed);
        CHECK_CASE(!memcmp(p.alpha, again.alpha, planes_count(&p)), seed);
        planes_free(&again);
        planes_free(&p);
        art_free(&art);
    }
}

/* At the source's own size every texel comes back exactly, at any offset. */
static void own_size_is_the_identity(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 40503u;
        Art art = art_random(&state);
        JelliRect source = art_source(&state, &art);
        Planes p = planes_make(source.width, source.height);
        CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset, source), seed);
        for (unsigned y = 0u; y < source.height; ++y)
            for (unsigned x = 0u; x < source.width; ++x) {
                size_t at = (size_t)y * source.width + x;
                size_t from = (size_t)(source.y + y) * art.width + source.x + x;
                uint8_t a = art_alpha(&art, (long)source.x + (long)x, (long)source.y + (long)y);
                CHECK_CASE(p.alpha[at] == a, seed);
                CHECK_CASE(p.pixels[at] == (a ? art.pixels[from] : 0u), seed);
            }
        planes_free(&p);
        art_free(&art);
    }
}

/* Every covered texel one colour, every clear texel garbage: whatever the stretch,
 * every visible output pixel is exactly that colour (no garbage, no channel carry). */
static void clear_texels_never_tint(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 97u;
        Art art = art_random(&state);
        uint16_t colour = (uint16_t)art_next(&state);
        if (seed % 4u == 0u)
            colour = seed % 8u ? 0xffffu : 0x0000u; /* every channel at its maximum, minimum */
        for (size_t i = 0u; i < (size_t)art.width * art.height; ++i)
            if (art_alpha(&art, (long)(i % art.width), (long)(i / art.width)))
                art.pixels[i] = colour;
        JelliRect source = art_source(&state, &art);
        Planes p = planes_make(art_size(&state, source.width), art_size(&state, source.height));
        CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset, source), seed);
        for (size_t i = 0u; i < planes_count(&p); ++i)
            CHECK_CASE(p.alpha[i] ? p.pixels[i] == colour : p.pixels[i] == 0u, seed);
        planes_free(&p);
        art_free(&art);
    }
}

/* Metamorphic: an image's edges behave as clear, so an asset resampled whole equals the
 * same texels set inside a larger clear canvas (garbage colour around) resampled from
 * their place in it. */
static void the_image_edge_is_clear(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 7919u;
        Art art = art_random(&state);
        unsigned ox = 1u + art_below(&state, 3u), oy = 1u + art_below(&state, 3u);
        Art canvas =
            art_make(&state, art.width + ox + 1u + art_below(&state, 3u),
                     art.height + oy + 1u + art_below(&state, 3u), 1u, !art.alpha, ART_EMPTY);
        for (unsigned y = 0u; y < art.height; ++y)
            for (unsigned x = 0u; x < art.width; ++x) {
                canvas.pixels[(size_t)(y + oy) * canvas.width + x + ox] =
                    art.pixels[(size_t)y * art.width + x];
                art_set_alpha(&canvas, x + ox, y + oy, art_alpha(&art, (long)x, (long)y));
            }
        unsigned w = art_size(&state, art.width), h = art_size(&state, art.height);
        Planes whole = planes_make(w, h), placed = planes_make(w, h);
        CHECK_CASE(jelli_agent_sprite_resample(&whole.sprite, &art.asset,
                                               (JelliRect){0u, 0u, art.width, art.height}),
                   seed);
        CHECK_CASE(jelli_agent_sprite_resample(&placed.sprite, &canvas.asset,
                                               (JelliRect){ox, oy, art.width, art.height}),
                   seed);
        CHECK_CASE(!memcmp(whole.alpha, placed.alpha, planes_count(&whole)), seed);
        CHECK_CASE(!memcmp(whole.pixels, placed.pixels, planes_count(&whole) * sizeof(uint16_t)),
                   seed);
        planes_free(&whole);
        planes_free(&placed);
        art_free(&canvas);
        art_free(&art);
    }
}

/* Metamorphic: the resampler works in stored pixels, so the same stored planes give the
 * same sprite at density 1 and 2, and a legacy mask gives what a 0/255 alpha plane does. */
static void density_and_mask_do_not_change_the_result(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 31337u;
        Art masked = art_make(&state, 1u + art_below(&state, 12u), 1u + art_below(&state, 12u), 2u,
                              true, ART_BINARY);
        Art plane = art_make(&state, masked.width / 2u, masked.height / 2u, 2u, false, ART_EMPTY);
        memcpy(plane.pixels, masked.pixels,
               (size_t)masked.width * masked.height * sizeof(uint16_t));
        for (unsigned y = 0u; y < masked.height; ++y)
            for (unsigned x = 0u; x < masked.width; ++x)
                art_set_alpha(&plane, x, y, art_alpha(&masked, (long)x, (long)y));
        JelliRect source = art_source(&state, &masked);
        unsigned w = art_size(&state, source.width), h = art_size(&state, source.height);
        Planes expected = planes_make(w, h), got = planes_make(w, h);
        CHECK_CASE(jelli_agent_sprite_resample(&expected.sprite, &plane.asset, source), seed);
        JelliAsset variants[3] = {masked.asset, plane.asset, masked.asset};
        variants[0].density = 2u; /* mask, density 2 */
        variants[1].density = 1u; /* alpha plane read as density 1 */
        variants[1].width = (uint16_t)masked.width;
        variants[1].height = (uint16_t)masked.height;
        variants[2] = variants[1]; /* mask read as density 0 (legacy) */
        variants[2].alpha = NULL;
        variants[2].mask = masked.mask;
        variants[2].density = 0u;
        for (unsigned v = 0u; v < 3u; ++v) {
            CHECK_CASE(jelli_agent_sprite_resample(&got.sprite, &variants[v], source), seed);
            CHECK_CASE(!memcmp(expected.alpha, got.alpha, planes_count(&got)), seed);
            CHECK_CASE(!memcmp(expected.pixels, got.pixels, planes_count(&got) * sizeof(uint16_t)),
                       seed);
        }
        planes_free(&expected);
        planes_free(&got);
        art_free(&plane);
        art_free(&masked);
    }
}

/* A source with at least one opaque texel around it on every side never comes back
 * thinner: every output pixel is fully opaque, at any ratio. */
static void an_opaque_interior_stays_opaque(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 65599u;
        Art art = art_make(&state, 3u + art_below(&state, 30u), 3u + art_below(&state, 30u),
                           art_below(&state, 2u), art_below(&state, 2u), ART_OPAQUE);
        unsigned w = 1u + art_below(&state, art.width - 2u),
                 h = 1u + art_below(&state, art.height - 2u);
        JelliRect source = {1u + art_below(&state, art.width - 1u - w),
                            1u + art_below(&state, art.height - 1u - h), w, h};
        Planes p = planes_make(art_size(&state, w), art_size(&state, h));
        CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset, source), seed);
        for (size_t i = 0u; i < planes_count(&p); ++i)
            CHECK_CASE(p.alpha[i] == 255u, seed);
        planes_free(&p);
        art_free(&art);
    }
}

/* Exact: halving the width samples midway between each pair of texels, so alpha is
 * their mean rounded half up and colour their alpha-weighted mean rounded to nearest. */
static void halving_averages_each_pair(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 1103515245u;
        Art art = art_make(&state, 2u * (1u + art_below(&state, 16u)), 1u + art_below(&state, 16u),
                           1u, false, (ArtCoverage)art_below(&state, ART_KINDS));
        Planes p = planes_make(art.width / 2u, art.height);
        CHECK_CASE(jelli_agent_sprite_resample(&p.sprite, &art.asset,
                                               (JelliRect){0u, 0u, art.width, art.height}),
                   seed);
        for (unsigned y = 0u; y < art.height; ++y)
            for (unsigned x = 0u; x < p.sprite.width; ++x) {
                size_t left = (size_t)y * art.width + 2u * (size_t)x,
                       at = (size_t)y * p.sprite.width + x;
                unsigned a0 = art.alpha[left], a1 = art.alpha[left + 1u], sum = a0 + a1;
                CHECK_CASE(p.alpha[at] == (sum + 1u) / 2u, seed);
                unsigned c0 = art.pixels[left], c1 = art.pixels[left + 1u], expected = 0u;
                for (unsigned k = 0u; sum && k < 3u; ++k) {
                    unsigned shift = k == 0u ? 11u : k == 1u ? 5u : 0u, bits = k == 1u ? 63u : 31u;
                    unsigned mean =
                        (2u * (((c0 >> shift) & bits) * a0 + ((c1 >> shift) & bits) * a1) + sum) /
                        (2u * sum);
                    expected |= mean << shift;
                }
                CHECK_CASE(p.pixels[at] == expected, seed);
            }
        planes_free(&p);
        art_free(&art);
    }
}

/* Huge ratios against the reference: one texel to the whole round panel (alone, and in
 * an opaque field), and a 2048-wide strip down to a few pixels. */
static void huge_ratios(void)
{
    uint32_t state = 11u;
    Art dot = art_make(&state, 3u, 3u, 1u, false, ART_EMPTY);
    art_set_alpha(&dot, 1u, 1u, 255u);
    Planes panel = planes_make(466u, 466u);
    CHECK_CASE(jelli_agent_sprite_resample(&panel.sprite, &dot.asset, (JelliRect){1u, 1u, 1u, 1u}),
               1u);
    compare_with_reference(&dot, (JelliRect){1u, 1u, 1u, 1u}, &panel, 1u);
    CHECK_CASE(panel.alpha[233u * 466u + 233u] >= 253u, 1u);      /* about the texel centre */
    CHECK_CASE(panel.alpha[0] < 255u && panel.alpha[0] > 0u, 1u); /* half a texel fades out */
    Art field = art_make(&state, 3u, 3u, 1u, true, ART_OPAQUE);
    CHECK_CASE(
        jelli_agent_sprite_resample(&panel.sprite, &field.asset, (JelliRect){1u, 1u, 1u, 1u}), 2u);
    for (size_t i = 0u; i < planes_count(&panel); ++i)
        CHECK_CASE(panel.alpha[i] == 255u, 2u);
    Art strip = art_make(&state, 1024u, 1u, 2u, false, ART_NOISE); /* 2048 x 2 stored */
    for (unsigned w = 1u; w <= 5u; ++w) {
        Planes small = planes_make(w, 1u);
        JelliRect source = {0u, 0u, strip.width, strip.height};
        CHECK_CASE(jelli_agent_sprite_resample(&small.sprite, &strip.asset, source), 3u + w);
        compare_with_reference(&strip, source, &small, 3u + w);
        planes_free(&small);
    }
    planes_free(&panel);
    art_free(&strip);
    art_free(&field);
    art_free(&dot);
}

int main(void)
{
    resample_matches_the_reference();
    own_size_is_the_identity();
    clear_texels_never_tint();
    the_image_edge_is_clear();
    density_and_mask_do_not_change_the_result();
    an_opaque_interior_stays_opaque();
    huge_ratios();
    halving_averages_each_pair();
    printf("Agent sprite resample properties passed: %u seeds each; worst alpha %.2f, channel %.2f "
           "from the reference\n",
           (unsigned)SEEDS, worst_alpha, worst_channel);
    return 0;
}
