/* Seeded fuzzing of the SDL live-artwork decoder (ports/sdl/asset_pack.c). An independent
 * writer packs all real embedded art as JLAP v2 (the legacy subset as v1); both round-trip
 * exactly, so the decoder's re-measured geometry agrees with the tooling on every sprite.
 * Mutants (header/record bit flips, lengths, density/reserved bytes, counts, duplicate
 * IDs, truncation, trailing bytes, payload noise) decode deterministically without a
 * crash (ASan in make sanitize); prefixes, extensions, count/length changes, reserved
 * bytes and duplicates are refused; anything accepted is bounded inside the bank with
 * unique embedded IDs and re-measured bounds; colour-only edits load. Prints the seed. */
#include "asset_reload.h"
#include "asset_reference.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_CASE(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x)\n", __FILE__, __LINE__, #expression,          \
                    (unsigned)current_seed);                                                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { HEADER = 16, MUTANTS = 3000, SMALL = 6 };
static uint32_t current_seed;
static unsigned accepted, refused, open_accepted; /* Mutant outcomes. */
static JelliAssetBank bank, again;
static uint8_t pack[JELLI_ASSET_PACK_CAPACITY + 64u], pristine[JELLI_ASSET_PACK_CAPACITY];
static uint32_t ids[JELLI_ASSET_PACK_COUNT];
static unsigned id_count;

typedef struct {
    size_t size, records[JELLI_ASSET_PACK_COUNT + 1u]; /* Record offsets, then the end. */
    unsigned count, version;
} Layout;

static size_t put16(uint8_t *out, size_t at, unsigned value)
{
    out[at] = (uint8_t)(value & 0xffu);
    out[at + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    return at + 2u;
}

static size_t put32(uint8_t *out, size_t at, uint32_t value)
{
    return put16(out, put16(out, at, value & 0xffffu), value >> 16u);
}

static size_t payload_size(const JelliAsset *a)
{
    if (a->id == 4001u)
        return (size_t)96u * 12u;
    size_t d = jelli_asset_density(a), count = (size_t)a->width * d * a->height * d;
    return a->alpha ? count * 3u : count * 2u + (size_t)(a->width + 7u) / 8u * (size_t)a->height;
}

static size_t write_payload(uint8_t *out, size_t at, const JelliAsset *a)
{
    if (a->id == 4001u) { /* Glyph-major font rows, codepoints 32..127. */
        for (unsigned code = 32u; code < 128u; ++code, at += 12u)
            memcpy(out + at, jelli_asset_glyph((uint8_t)code), 12u);
        return at;
    }
    size_t d = jelli_asset_density(a), count = (size_t)a->width * d * a->height * d;
    for (size_t i = 0u; i < count; ++i)
        at = put16(out, at, a->pixels[i]);
    if (a->alpha) {
        memcpy(out + at, a->alpha, count);
        return at + count;
    }
    size_t stride = (size_t)(a->width + 7u) / 8u; /* Packs carry tight mask rows. */
    for (unsigned row = 0u; row < a->height; ++row, at += stride)
        memcpy(out + at, a->mask + (size_t)row * a->mask_stride, stride);
    return at;
}

static size_t write_record(uint8_t *out, size_t at, const JelliAsset *a, unsigned version)
{
    at = put32(out, at, a->id);
    at = put16(out, put16(out, at, a->width), a->height);
    at = put16(out, put16(out, at, a->ground_x_q8), a->ground_y_q8);
    at = put16(out, put16(out, at, a->centroid_x_q8), a->centroid_y_q8);
    const uint8_t bounds[4] = {a->left, a->top, a->right, a->bottom};
    memcpy(out + at, bounds, 4u);
    at = put32(out, at + 4u, (uint32_t)payload_size(a));
    if (version == 2u) {
        const uint8_t tail[4] = {(uint8_t)jelli_asset_density(a), 0u, 0u, 0u};
        memcpy(out + at, tail, 4u);
        at += 4u;
    }
    return write_payload(out, at, a);
}

static Layout build(uint8_t *out, const uint32_t *list, unsigned count, unsigned version)
{
    Layout layout = {.count = count, .version = version};
    memcpy(out, "JLAP", 4u);
    put32(out, 4u, version);
    put32(out, 8u, count);
    put32(out, 12u, 0u); /* The reload host checks the CRC, not the decoder. */
    size_t at = HEADER;
    for (unsigned i = 0u; i < count; ++i) {
        layout.records[i] = at;
        at = write_record(out, at, jelli_asset_find(list[i]), version);
    }
    layout.records[count] = layout.size = at;
    return layout;
}

static bool same_metadata(const JelliAsset *got, const JelliAsset *want)
{
    return got->id == want->id && got->width == want->width && got->height == want->height &&
           jelli_asset_density(got) == jelli_asset_density(want) &&
           got->ground_x_q8 == want->ground_x_q8 && got->ground_y_q8 == want->ground_y_q8 &&
           got->centroid_x_q8 == want->centroid_x_q8 && got->centroid_y_q8 == want->centroid_y_q8 &&
           got->left == want->left && got->top == want->top && got->right == want->right &&
           got->bottom == want->bottom;
}

static bool same_asset(const JelliAsset *got, const JelliAsset *want)
{
    unsigned d = jelli_asset_density(want);
    size_t count = (size_t)want->width * d * want->height * d;
    bool meta = same_metadata(got, want);
    if (!meta || want->id == 4001u)
        return meta;
    if (memcmp(got->pixels, want->pixels, count * sizeof(uint16_t)) != 0)
        return false;
    if (want->alpha)
        return got->alpha && !got->mask && memcmp(got->alpha, want->alpha, count) == 0;
    for (unsigned y = 0u; y < want->height * d; ++y)
        for (unsigned x = 0u; x < want->width * d; ++x)
            if (jelli_asset_alpha_at(got, x, y) != jelli_asset_alpha_at(want, x, y))
                return false;
    return true;
}

/* Every embedded asset, v2; then the legacy subset as v1. */
static void real_art_round_trips(void)
{
    for (uint32_t id = 1u; id < 20000u && id_count < JELLI_ASSET_PACK_COUNT; ++id)
        if (jelli_asset_find(id))
            ids[id_count++] = id;
    CHECK_CASE(id_count == 102u);
    Layout layout = build(pack, ids, id_count, 2u);
    CHECK_CASE(jelli_sdl_asset_pack_decode(&bank, pack, layout.size));
    CHECK_CASE(bank.set.items == bank.descriptors && bank.set.count == id_count);
    CHECK_CASE(bank.set.glyphs == bank.glyphs);
    for (unsigned code = 32u; code < 128u; ++code)
        CHECK_CASE(memcmp(jelli_asset_lookup_glyph(&bank.set, (uint8_t)code),
                          jelli_asset_glyph((uint8_t)code), 12u) == 0);
    uint32_t legacy[JELLI_ASSET_PACK_COUNT];
    unsigned hires = 0u, legacy_count = 0u;
    for (unsigned i = 0u; i < id_count; ++i) {
        CHECK_CASE(same_asset(&bank.descriptors[i], jelli_asset_find(ids[i])));
        if (jelli_asset_density(&bank.descriptors[i]) > 1u)
            ++hires;
        else
            legacy[legacy_count++] = ids[i];
    }
    CHECK_CASE(hires == 95u);
    layout = build(pack, legacy, legacy_count, 1u);
    CHECK_CASE(legacy_count == 7u && jelli_sdl_asset_pack_decode(&bank, pack, layout.size));
    for (unsigned i = 0u; i < legacy_count; ++i)
        CHECK_CASE(same_asset(&bank.descriptors[i], jelli_asset_find(legacy[i])));
    printf("round trip: %u shipped assets (%u high-resolution) as v2, %u legacy as v1\n", id_count,
           hires, legacy_count);
}

static bool inside(const void *start, size_t bytes, const void *base, size_t capacity)
{
    const uint8_t *p = start, *b = base;
    return p >= b && (size_t)(p - b) <= capacity && bytes <= capacity - (size_t)(p - b);
}

/* Whatever the decoder accepts is bounded, unique, embedded and self-consistent. */
static void check_accepted(const JelliAssetBank *b)
{
    CHECK_CASE(b->set.count >= 1u && b->set.count <= JELLI_ASSET_PACK_COUNT);
    for (size_t i = 0u; i < b->set.count; ++i) {
        const JelliAsset *a = &b->descriptors[i], *original = jelli_asset_find(a->id);
        CHECK_CASE(original && a->width == original->width && a->height == original->height);
        for (size_t j = 0u; j < i; ++j)
            CHECK_CASE(b->descriptors[j].id != a->id);
        if (a->id == 4001u)
            continue;
        unsigned d = ref_density(a);
        size_t count = (size_t)a->width * d * a->height * d;
        CHECK_CASE(d >= 1u && (a->alpha != NULL) != (a->mask != NULL));
        CHECK_CASE(inside(a->pixels, count * 2u, b->pixels, sizeof(b->pixels)));
        if (a->alpha)
            CHECK_CASE(inside(a->alpha, count, b->alpha, sizeof(b->alpha)));
        else
            CHECK_CASE(
                a->mask_stride == (a->width + 7u) / 8u &&
                inside(a->mask, (size_t)a->mask_stride * a->height, b->masks, sizeof(b->masks)));
        CHECK_CASE(ref_bounds_match(a));
    }
}

typedef enum { REJECT, ACCEPT, EITHER } Expect;

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

/* Rewrites one record's payload length or density/reserved tail; both must refuse. */
static Expect mutate_record(Layout *l, RefRandom *r, unsigned kind)
{
    size_t record = l->records[ref_below(r, l->count)];
    if (kind == 0u) { /* Payload length: off by one either way, zero or huge. */
        uint32_t length = read32(pack + record + 20u);
        const uint32_t values[] = {length + 1u, length - 1u, 0u, 0xffffffffu};
        put32(pack, record + 20u, values[ref_below(r, 4u)]); /* Payloads are never empty. */
        return REJECT;
    }
    if (l->version != 2u)
        return EITHER;
    unsigned at = 24u + ref_below(r, 4u);
    if (at == 24u) {
        uint8_t density = pack[record + 24u];
        pack[record + 24u] = (uint8_t)(density ^ (1u << ref_below(r, 8u)));
    } else {
        pack[record + at] = (uint8_t)(1u + ref_below(r, 255u)); /* Reserved: must be zero. */
    }
    return REJECT;
}

/* A colour-only edit of a pixel plane keeps every coverage measure, so it must load. */
static Expect mutate_color(Layout *l, RefRandom *r, size_t *offset, uint16_t *value)
{
    unsigned index = ref_below(r, l->count);
    size_t record = l->records[index], body = record + (l->version == 2u ? 28u : 24u);
    const JelliAsset *a = jelli_asset_find(read32(pack + record));
    if (!a || a->id == 4001u) /* An earlier edit may have renamed the record. */
        return EITHER;
    unsigned d = jelli_asset_density(a);
    size_t count = (size_t)a->width * d * a->height * d, pixel = ref_below(r, (unsigned)count);
    *offset = (size_t)index * 0x100000u + pixel;
    *value = (uint16_t)ref_next(r);
    put16(pack, body + pixel * 2u, *value);
    return ACCEPT;
}

static Expect mutate(Layout *l, RefRandom *r, size_t *size, size_t *offset, uint16_t *value)
{
    unsigned kind = ref_below(r, 11u);
    if (kind == 0u) { /* Magic, version or count bit: always refused. */
        unsigned bit = ref_below(r, 96u);
        pack[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        return REJECT;
    }
    if (kind == 1u) { /* The CRC field is the host's; the decoder ignores it. */
        pack[12u + ref_below(r, 4u)] ^= (uint8_t)(1u << ref_below(r, 8u));
        return ACCEPT;
    }
    if (kind == 2u || kind == 3u)
        return mutate_record(l, r, kind - 2u);
    if (kind == 4u) { /* Any bit of any record header: robustness only. */
        size_t record = l->records[ref_below(r, l->count)];
        pack[record + ref_below(r, l->version == 2u ? 28u : 24u)] ^=
            (uint8_t)(1u << ref_below(r, 8u));
        return EITHER;
    }
    if (kind == 5u) { /* Strict prefix: always incomplete. */
        *size = ref_below(r, (unsigned)*size);
        return REJECT;
    }
    if (kind == 6u) { /* Trailing bytes after the last record. */
        unsigned extra = 1u + ref_below(r, 64u);
        for (unsigned i = 0u; i < extra; ++i)
            pack[*size + i] = (uint8_t)ref_next(r);
        *size += extra;
        return REJECT;
    }
    if (kind == 7u) { /* Count off by one, zero, past the bound, or huge. */
        const uint32_t counts[] = {l->count + 1u, l->count - 1u, 0u, JELLI_ASSET_PACK_COUNT + 1u,
                                   0xffffffffu};
        put32(pack, 8u, counts[ref_below(r, 5u)]);
        return REJECT;
    }
    if (kind == 8u && l->count > 1u) { /* A later record repeats an earlier ID. */
        unsigned later = 1u + ref_below(r, l->count - 1u), earlier = ref_below(r, later);
        memcpy(pack + l->records[later], pack + l->records[earlier], 4u);
        return REJECT;
    }
    if (kind == 9u) { /* Coverage noise in a payload: the geometry check decides. */
        size_t at = l->records[0] + ref_below(r, (unsigned)(*size - l->records[0]));
        pack[at] = (uint8_t)ref_next(r);
        return EITHER;
    }
    return mutate_color(l, r, offset, value);
}

/* Two decodes of the same bytes agree on the result and on every descriptor. */
static bool decode_twice(size_t size)
{
    bool ok = jelli_sdl_asset_pack_decode(&bank, pack, size);
    CHECK_CASE(ok == jelli_sdl_asset_pack_decode(&again, pack, size));
    if (!ok)
        return false;
    CHECK_CASE(bank.set.count == again.set.count);
    for (size_t i = 0u; i < bank.set.count; ++i) {
        const JelliAsset *a = &bank.descriptors[i], *b = &again.descriptors[i];
        CHECK_CASE(a->id == b->id && a->density == b->density && a->left == b->left &&
                   a->centroid_x_q8 == b->centroid_x_q8 && a->ground_y_q8 == b->ground_y_q8);
        CHECK_CASE((a->pixels ? a->pixels - bank.pixels : -1) ==
                   (b->pixels ? b->pixels - again.pixels : -1));
    }
    return true;
}

/* One to six distinct assets (legacy only for v1), or now and then the full set. */
static void small_pack(RefRandom *r, Layout *layout)
{
    unsigned version = ref_below(r, 4u) == 0u ? 1u : 2u, pool_count = 0u;
    uint32_t pool[JELLI_ASSET_PACK_COUNT] = {0}, chosen[SMALL] = {0};
    for (unsigned i = 0u; i < id_count; ++i)
        if (version == 2u || jelli_asset_density(jelli_asset_find(ids[i])) == 1u)
            pool[pool_count++] = ids[i];
    if (version == 2u && ref_below(r, 200u) == 0u) {
        *layout = build(pack, ids, id_count, 2u);
        return;
    }
    unsigned count = 1u + ref_below(r, SMALL < pool_count ? SMALL : pool_count);
    for (unsigned i = 0u; i < count; ++i) { /* Partial Fisher-Yates: distinct IDs. */
        unsigned j = i + ref_below(r, pool_count - i);
        uint32_t swap = pool[j];
        pool[j] = pool[i];
        chosen[i] = pool[i] = swap;
    }
    *layout = build(pack, chosen, count, version);
}

static void one_mutant(unsigned i)
{
    current_seed = 0xf0220000u + i;
    RefRandom r = {current_seed * 2654435761u | 1u};
    Layout layout;
    small_pack(&r, &layout);
    size_t size = layout.size, offset = 0u;
    uint16_t value = 0u;
    CHECK_CASE(jelli_sdl_asset_pack_decode(&bank, pack, size)); /* Unmutated packs load. */
    unsigned rounds = ref_below(&r, 4u) == 0u ? 2u + ref_below(&r, 2u) : 1u;
    Expect expect = EITHER;
    for (unsigned m = 0u; m < rounds; ++m)
        expect = mutate(&layout, &r, &size, &offset, &value);
    expect = rounds > 1u ? EITHER : expect; /* Several edits together: robustness only. */
    bool ok = decode_twice(size);
    CHECK_CASE(expect == EITHER || ok == (expect == ACCEPT));
    if (ok) {
        check_accepted(&bank);
        if (expect == ACCEPT && value)
            CHECK_CASE(bank.descriptors[offset >> 20u].pixels[offset & 0xfffffu] == value);
    }
    accepted += ok ? 1u : 0u;
    refused += ok ? 0u : 1u;
    open_accepted += ok && expect == EITHER ? 1u : 0u;
}

static void mutants_are_refused_or_bounded(void)
{
    for (unsigned i = 0u; i < MUTANTS; ++i)
        one_mutant(i);
    CHECK_CASE(accepted > MUTANTS / 10u && refused > MUTANTS / 2u);
    printf("pack fuzz: %u mutants, %u refused, %u accepted (%u open-ended edits), all bounded\n",
           (unsigned)MUTANTS, refused, accepted, open_accepted);
}

/* Header-level limits: tiny, oversized, missing buffers, and an unknown version. */
static void limits(void)
{
    current_seed = 0u;
    Layout layout = build(pack, ids, 3u, 2u);
    CHECK_CASE(jelli_sdl_asset_pack_decode(&bank, pack, layout.size));
    for (size_t size = 0u; size < HEADER; ++size)
        CHECK_CASE(!jelli_sdl_asset_pack_decode(&bank, pack, size));
    CHECK_CASE(!jelli_sdl_asset_pack_decode(NULL, pack, layout.size) &&
               !jelli_sdl_asset_pack_decode(&bank, NULL, layout.size));
    memset(pack + layout.size, 0, sizeof(pack) - layout.size);
    CHECK_CASE(!jelli_sdl_asset_pack_decode(&bank, pack, JELLI_ASSET_PACK_CAPACITY + 1u));
    for (uint32_t version = 0u; version < 8u; ++version) {
        put32(pack, 4u, version);
        CHECK_CASE(jelli_sdl_asset_pack_decode(&bank, pack, layout.size) == (version == 2u));
    }
    memcpy(pristine, pack + 8u, layout.size - 8u); /* Decoding never writes the input. */
    CHECK_CASE(!jelli_sdl_asset_pack_decode(&bank, pack, layout.size));
    CHECK_CASE(memcmp(pristine, pack + 8u, layout.size - 8u) == 0);
}

int main(void)
{
    real_art_round_trips();
    limits();
    mutants_are_refused_or_bounded();
    puts("PASS: JLAP v1/v2 round trips on the real art; mutants refused or bounded");
    return 0;
}
