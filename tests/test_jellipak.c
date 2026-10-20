#include "jelli/jellipak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Independently authored raw 'hello' and LZ4: four literals abcd, distance four
 * match of four, then five literals efghi. CRC bytes checked with Python zlib;
 * no pack compiler or compressor participates in these expected wire bytes. */
static const unsigned char fixture[] = {
    0x4a, 0x50, 0x41, 0x4b, 0x01, 0x00, 0x20, 0x00, 0x72, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x3e, 0x2d, 0xda, 0xbb, 0xbf, 0x06,
    0x44, 0xf3, 0x01, 0x00, 0x00, 0x00, 0x42, 0x4c, 0x4f, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x60,
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x86, 0xa6, 0x10, 0x36,
    0x86, 0xa6, 0x10, 0x36, 0x02, 0x00, 0x00, 0x00, 0x42, 0x4c, 0x4f, 0x4b, 0x01, 0x00, 0x00,
    0x00, 0x65, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x75, 0x04,
    0x60, 0x21, 0xfc, 0x7f, 0xe2, 0x62, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x40, 0x61, 0x62, 0x63,
    0x64, 0x04, 0x00, 0x50, 0x65, 0x66, 0x67, 0x68, 0x69};
static const JelliPakLimits limits = {sizeof(fixture), 2U, 13U, 13U};

static void put32(unsigned char *p, uint32_t value)
{
    for (unsigned i = 0U; i < 4U; ++i)
        p[i] = (unsigned char)(value >> (i * 8U));
}

static void repair(unsigned char *data)
{
    put32(data + 24U, jelli_jellipak_crc32(data + 32U, 64U));
    put32(data + 28U, jelli_jellipak_crc32(data, 28U));
}

static JelliPakSource source(JelliPakMemory *memory)
{
    return (JelliPakSource){memory, memory->size, jelli_jellipak_memory_read};
}

static void known_answer(void)
{
    unsigned char unaligned[sizeof(fixture) + 1U];
    memcpy(unaligned + 1U, fixture, sizeof(fixture));
    JelliPakMemory memory = {unaligned + 1U, sizeof(fixture)};
    JelliPak pak;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    CHECK(pak.block_count == 2U);
    unsigned char scratch[13], decoded[13];
    size_t written = 99U;
    CHECK(jelli_jellipak_crc32((const unsigned char *)"123456789", 9U) == 0xcbf43926U);
    CHECK(jelli_jellipak_decode(&pak, 0U, (JelliByteSpan){scratch, 5U},
                                (JelliByteSpan){decoded, 5U}, &written) == JELLI_PAK_OK);
    CHECK(written == 5U && memcmp(decoded, "hello", 5U) == 0);
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){decoded, 13U}, &written) == JELLI_PAK_OK);
    CHECK(written == 13U && memcmp(decoded, "abcdabcdefghi", 13U) == 0);
    JelliPakBlock block;
    CHECK(jelli_jellipak_block(&pak, 1U, &block) == JELLI_PAK_OK);
    CHECK(block.id == 2U && block.offset == 101U && block.codec == 1U);
}

static void mount_mutations(void)
{
    static const struct {
        size_t offset;
        uint32_t value;
        JelliPakStatus error;
    } cases[] = {{12U, UINT32_MAX, JELLI_PAK_CAPACITY},
                 {16U, 33U, JELLI_PAK_FORMAT},
                 {20U, 31U, JELLI_PAK_FORMAT},
                 {32U, 0U, JELLI_PAK_FORMAT},
                 {64U, 1U, JELLI_PAK_FORMAT},
                 {36U, 0U, JELLI_PAK_FORMAT},
                 {40U, 2U, JELLI_PAK_CODEC},
                 {44U, 95U, JELLI_PAK_FORMAT},
                 {76U, 100U, JELLI_PAK_FORMAT},
                 {48U, 0U, JELLI_PAK_FORMAT},
                 {52U, 4U, JELLI_PAK_FORMAT},
                 {80U, 14U, JELLI_PAK_CAPACITY},
                 {84U, 14U, JELLI_PAK_CAPACITY}};
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        unsigned char bytes[sizeof(fixture)];
        memcpy(bytes, fixture, sizeof(bytes));
        put32(bytes + cases[i].offset, cases[i].value);
        repair(bytes);
        JelliPakMemory memory = {bytes, sizeof(bytes)};
        JelliPak pak = {0};
        CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == cases[i].error);
        CHECK(pak.source.context == NULL && pak.source.size == 0U && pak.source.read_at == NULL);
        CHECK(pak.limits.max_pack_size == 0U && pak.limits.max_blocks == 0U);
        CHECK(pak.limits.max_stored_block == 0U && pak.limits.max_decoded_block == 0U);
        CHECK(pak.block_count == 0U);
    }
}

static void truncation_and_integrity(void)
{
    JelliPak pak;
    for (size_t size = 0U; size < sizeof(fixture); ++size) {
        JelliPakMemory memory = {fixture, size};
        CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) != JELLI_PAK_OK);
    }
    for (size_t i = 0U; i < 96U; ++i) {
        unsigned char bytes[sizeof(fixture)];
        memcpy(bytes, fixture, sizeof(bytes));
        bytes[i] ^= 0x80U;
        JelliPakMemory memory = {bytes, sizeof(bytes)};
        CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) != JELLI_PAK_OK);
    }
}

static void decode_failures(void)
{
    unsigned char bytes[sizeof(fixture)], scratch[13], output[13];
    memcpy(bytes, fixture, sizeof(bytes));
    JelliPakMemory memory = {bytes, sizeof(bytes)};
    JelliPak pak;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    size_t written = 123U;
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 12U},
                                (JelliByteSpan){output, 13U}, &written) == JELLI_PAK_CAPACITY);
    CHECK(written == 123U);
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){output, 12U}, &written) == JELLI_PAK_CAPACITY);
    CHECK(written == 123U);
    bytes[96U] ^= 1U;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    CHECK(jelli_jellipak_decode(&pak, 0U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){output, 13U}, &written) == JELLI_PAK_INTEGRITY);
    CHECK(written == 123U);
    bytes[106U] = 0U; /* LZ4 zero match distance, with stored CRC repaired. */
    put32(bytes + 88U, jelli_jellipak_crc32(bytes + 101U, 13U));
    repair(bytes);
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){output, 13U}, &written) == JELLI_PAK_DECODE);
    CHECK(written == 123U);
}

typedef struct {
    size_t boundary;
} FailingSource;

/* Match the shared source callback, including mutable transport contexts. */
// cppcheck-suppress constParameterCallback
static bool short_read(void *context, size_t offset, unsigned char *out, size_t size)
{
    const FailingSource *failure = context;
    if (offset > failure->boundary || size > failure->boundary - offset)
        return false;
    memcpy(out, fixture + offset, size);
    return true;
}

static void source_failures(void)
{
    for (size_t end = 0U; end < 96U; ++end) {
        FailingSource failure = {end};
        JelliPak pak;
        CHECK(jelli_jellipak_mount((JelliPakSource){&failure, sizeof(fixture), short_read}, limits,
                                   &pak) == JELLI_PAK_IO);
    }
    for (size_t end = 96U; end < sizeof(fixture); ++end) {
        FailingSource failure = {end};
        JelliPak pak;
        CHECK(jelli_jellipak_mount((JelliPakSource){&failure, sizeof(fixture), short_read}, limits,
                                   &pak) == JELLI_PAK_OK);
        unsigned char scratch[13], decoded[13];
        size_t written = 77U;
        uint32_t index = end < 101U ? 0U : 1U;
        CHECK(jelli_jellipak_decode(&pak, index, (JelliByteSpan){scratch, 13U},
                                    (JelliByteSpan){decoded, 13U}, &written) == JELLI_PAK_IO);
        CHECK(written == 77U);
    }
}

static void decoded_crc_and_limits(void)
{
    unsigned char bytes[sizeof(fixture)];
    memcpy(bytes, fixture, sizeof(bytes));
    bytes[92U] ^= 1U;
    repair(bytes);
    JelliPakMemory memory = {bytes, sizeof(bytes)};
    JelliPak pak;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    unsigned char scratch[13], decoded[13];
    size_t written = 77U;
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){decoded, 13U}, &written) == JELLI_PAK_INTEGRITY);
    CHECK(written == 77U);
    for (unsigned field = 0U; field < 4U; ++field) {
        JelliPakLimits small = limits;
        if (field == 0U)
            --small.max_pack_size;
        if (field == 1U)
            --small.max_blocks;
        if (field == 2U)
            --small.max_stored_block;
        if (field == 3U)
            --small.max_decoded_block;
        CHECK(jelli_jellipak_mount(source(&memory), small, &pak) == JELLI_PAK_CAPACITY);
    }
}

static void malformed_lz4(void)
{
    static const struct {
        size_t offset;
        unsigned char value;
    } mutations[] = {
        {101U, 0xf0U}, /* Extended literal length exceeds stored input. */
        {101U, 0x41U}, /* Match produces one excess output byte. */
        {106U, 5U},    /* Match references before decoded beginning. */
        {107U, 1U},    /* High distance byte also references before beginning. */
        {108U, 0x40U}, /* Final sequence leaves unwanted trailing input. */
        {108U, 0x60U}  /* Final literal length exceeds available input. */
    };
    for (size_t i = 0U; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        unsigned char bytes[sizeof(fixture)], scratch[13], output[13];
        memcpy(bytes, fixture, sizeof(bytes));
        bytes[mutations[i].offset] = mutations[i].value;
        put32(bytes + 88U, jelli_jellipak_crc32(bytes + 101U, 13U));
        repair(bytes);
        JelliPakMemory memory = {bytes, sizeof(bytes)};
        JelliPak pak;
        CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
        size_t written = 77U;
        CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 13U},
                                    (JelliByteSpan){output, 13U}, &written) == JELLI_PAK_DECODE);
        CHECK(written == 77U);
    }
}

static void empty_and_invalid(void)
{
    unsigned char empty[32] = {'J', 'P', 'A', 'K', 1U, 0U, 32U};
    put32(empty + 8U, 32U);
    put32(empty + 16U, 32U);
    put32(empty + 20U, 32U);
    put32(empty + 28U, jelli_jellipak_crc32(empty, 28U));
    JelliPakMemory memory = {empty, sizeof(empty)};
    JelliPak pak;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    CHECK(pak.block_count == 0U);
    JelliPakBlock block = {0};
    CHECK(jelli_jellipak_block(&pak, 0U, &block) == JELLI_PAK_INVALID);
    CHECK(jelli_jellipak_mount(source(&memory), limits, NULL) == JELLI_PAK_INVALID);
    CHECK(jelli_jellipak_mount((JelliPakSource){0}, limits, &pak) == JELLI_PAK_INVALID);
    CHECK(jelli_jellipak_block(NULL, 0U, &block) == JELLI_PAK_INVALID);
    CHECK(!jelli_jellipak_memory_read(NULL, 0U, empty, 1U));
    CHECK(!jelli_jellipak_memory_read(&memory, SIZE_MAX, empty, 1U));
    CHECK(jelli_jellipak_memory_read(&memory, sizeof(empty), NULL, 0U));
}

static void boundaries_and_block_io(void)
{
    JelliPakMemory memory = {fixture, sizeof(fixture)};
    JelliPak pak;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    unsigned char scratch[15], output[15];
    memset(scratch, 0xa5, sizeof(scratch));
    memset(output, 0xa5, sizeof(output));
    size_t written = 77U;
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch + 1U, 13U},
                                (JelliByteSpan){output + 1U, 13U}, &written) == JELLI_PAK_OK);
    CHECK(scratch[0] == 0xa5U && scratch[14] == 0xa5U);
    CHECK(output[0] == 0xa5U && output[14] == 0xa5U);
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){NULL, 1U}, (JelliByteSpan){output, 13U},
                                &written) == JELLI_PAK_INVALID);
    CHECK(jelli_jellipak_decode(&pak, 1U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){output, 13U}, NULL) == JELLI_PAK_INVALID);
}

static void small_lz4(const unsigned char *encoded, size_t encoded_size, const char *expected,
                      size_t decoded_size, JelliPakStatus status)
{
    unsigned char bytes[96] = {'J', 'P', 'A', 'K', 1U, 0U, 32U};
    CHECK(encoded_size <= sizeof(bytes) - 64U);
    put32(bytes + 8U, (uint32_t)(64U + encoded_size));
    put32(bytes + 12U, 1U);
    put32(bytes + 16U, 32U);
    put32(bytes + 20U, 32U);
    put32(bytes + 32U, 1U);
    put32(bytes + 36U, JELLI_PAK_BLOCK_TYPE);
    put32(bytes + 40U, 1U);
    put32(bytes + 44U, 64U);
    put32(bytes + 48U, (uint32_t)encoded_size);
    put32(bytes + 52U, (uint32_t)decoded_size);
    put32(bytes + 56U, jelli_jellipak_crc32(encoded, encoded_size));
    put32(bytes + 60U, jelli_jellipak_crc32((const unsigned char *)expected, decoded_size));
    memcpy(bytes + 64U, encoded, encoded_size);
    put32(bytes + 24U, jelli_jellipak_crc32(bytes + 32U, 32U));
    put32(bytes + 28U, jelli_jellipak_crc32(bytes, 28U));
    JelliPakMemory memory = {bytes, 64U + encoded_size};
    JelliPak pak;
    CHECK(jelli_jellipak_mount(source(&memory), limits, &pak) == JELLI_PAK_OK);
    unsigned char scratch[13], output[13];
    size_t written = 77U;
    CHECK(jelli_jellipak_decode(&pak, 0U, (JelliByteSpan){scratch, 13U},
                                (JelliByteSpan){output, 13U}, &written) == status);
    if (status == JELLI_PAK_OK)
        CHECK(written == decoded_size && memcmp(output, expected, written) == 0);
    else
        CHECK(written == 77U);
}

static void lz4_boundaries(void)
{
    static const unsigned char match_extension[] = {0x1fU, 'A', 1U, 0U, 0xffU};
    static const unsigned char short_distance[] = {0x10U, 'A', 1U};
    static const unsigned char ignored_nibble[] = {0x11U, 'A'};
    static const unsigned char overlap[] = {0x14U, 'A', 1U, 0U, 0x10U, 'B'};
    CHECK(jelli_jellipak_crc32(NULL, 0U) == 0U);
    small_lz4(match_extension, sizeof(match_extension), "A", 1U, JELLI_PAK_DECODE);
    small_lz4(short_distance, sizeof(short_distance), "A", 1U, JELLI_PAK_DECODE);
    small_lz4(ignored_nibble, sizeof(ignored_nibble), "A", 1U, JELLI_PAK_OK);
    small_lz4(overlap, sizeof(overlap), "AAAAAAAAAB", 10U, JELLI_PAK_OK);
}

int main(void)
{
    known_answer();
    lz4_boundaries();
    boundaries_and_block_io();
    malformed_lz4();
    empty_and_invalid();
    mount_mutations();
    truncation_and_integrity();
    decode_failures();
    source_failures();
    decoded_crc_and_limits();
    puts("jellipak tests passed");
    return 0;
}
