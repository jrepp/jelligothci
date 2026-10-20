#include "jelli/jellipak.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host verification policy, independent of untrusted directory lengths. */
#define VERIFY_BLOCK_LIMIT (1024U * 1024U)
#define VERIFY_COUNT_LIMIT 65536U
#define VERIFY_PACK_LIMIT (256U * 1024U * 1024U)

static bool file_read(void *context, size_t offset, unsigned char *out, size_t size)
{
    FILE *file = context;
    if (offset > (size_t)LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0)
        return false;
    return fread(out, 1U, size, file) == size;
}

/* JRAW v1: magic, u32 version/count, then u32 id/length and bytes per block. */
static uint32_t read_u32_le(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static bool compare_header(FILE *expected, uint32_t count)
{
    unsigned char header[12];
    if (fread(header, 1U, sizeof(header), expected) != sizeof(header)) {
        fprintf(stderr, "expected header truncated or unreadable\n");
        return false;
    }
    if (memcmp(header, "JRAW", 4U) != 0 || read_u32_le(header + 4U) != 1U ||
        read_u32_le(header + 8U) != count) {
        fprintf(stderr, "expected header magic, version or block count mismatch\n");
        return false;
    }
    return true;
}

static bool compare_block(FILE *expected, const JelliPakBlock *block, const unsigned char *decoded,
                          size_t written)
{
    unsigned char header[8];
    unsigned char chunk[1024];
    if (fread(header, 1U, sizeof(header), expected) != sizeof(header)) {
        fprintf(stderr, "block=%u expected record truncated or unreadable\n", (unsigned)block->id);
        return false;
    }
    if (read_u32_le(header) != block->id || read_u32_le(header + 4U) != block->decoded_size ||
        written != block->decoded_size) {
        fprintf(stderr, "block=%u expected id or length mismatch\n", (unsigned)block->id);
        return false;
    }
    for (size_t offset = 0U; offset < written;) {
        size_t count = written - offset;
        if (count > sizeof(chunk))
            count = sizeof(chunk);
        if (fread(chunk, 1U, count, expected) != count) {
            fprintf(stderr, "block=%u byteoffset=%zu expected bytes truncated or unreadable\n",
                    (unsigned)block->id, offset);
            return false;
        }
        for (size_t i = 0U; i < count; ++i) {
            if (chunk[i] != decoded[offset + i]) {
                fprintf(stderr, "block=%u byteoffset=%zu expected byte mismatch\n",
                        (unsigned)block->id, offset + i);
                return false;
            }
        }
        offset += count;
    }
    return true;
}

static bool compare_end(FILE *expected)
{
    if (fgetc(expected) != EOF || ferror(expected)) {
        fprintf(stderr, "expected stream has trailing bytes or read error\n");
        return false;
    }
    return true;
}

static int verify(FILE *file, size_t size, unsigned char *scratch, unsigned char *decoded,
                  FILE *expected)
{
    const JelliPakLimits limits = {VERIFY_PACK_LIMIT, VERIFY_COUNT_LIMIT, VERIFY_BLOCK_LIMIT,
                                   VERIFY_BLOCK_LIMIT};
    JelliPak pak;
    JelliPakStatus status =
        jelli_jellipak_mount((JelliPakSource){file, size, file_read}, limits, &pak);
    size_t stored_total = 0U, decoded_total = 0U;
    if (status != JELLI_PAK_OK) {
        fprintf(stderr, "mount error=%u\n", (unsigned)status);
        return 1;
    }
    if (expected && !compare_header(expected, pak.block_count))
        return 1;
    for (uint32_t i = 0U; i < pak.block_count; ++i) {
        JelliPakBlock block;
        size_t written = 0U;
        status = jelli_jellipak_block(&pak, i, &block);
        if (status == JELLI_PAK_OK)
            status = jelli_jellipak_decode(&pak, i, (JelliByteSpan){scratch, VERIFY_BLOCK_LIMIT},
                                           (JelliByteSpan){decoded, VERIFY_BLOCK_LIMIT}, &written);
        if (status == JELLI_PAK_OK &&
            (stored_total > SIZE_MAX - block.stored_size || decoded_total > SIZE_MAX - written)) {
            status = JELLI_PAK_CAPACITY;
        }
        if (status != JELLI_PAK_OK) {
            fprintf(stderr, "block=%u error=%u\n", (unsigned)i, (unsigned)status);
            return 1;
        }
        if (expected && !compare_block(expected, &block, decoded, written))
            return 1;
        stored_total += block.stored_size;
        decoded_total += written;
    }
    if (expected && !compare_end(expected))
        return 1;
    if (expected)
        printf("compared_bytes=%zu\n", decoded_total);
    printf("blocks=%u stored_bytes=%zu decoded_bytes=%zu pack_bytes=%zu\n",
           (unsigned)pak.block_count, stored_total, decoded_total, size);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 && (argc != 4 || strcmp(argv[2], "--compare") != 0)) {
        fprintf(stderr, "usage: jelli_jellipak_verify PACK [--compare EXPECTED]\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        fprintf(stderr, "cannot open pack\n");
        return 1;
    }
    FILE *expected = argc == 4 ? fopen(argv[3], "rb") : NULL;
    if (argc == 4 && !expected) {
        fprintf(stderr, "cannot open expected stream\n");
        (void)fclose(file);
        return 1;
    }
    long length = -1L;
    if (fseek(file, 0L, SEEK_END) == 0)
        length = ftell(file);
    unsigned char *scratch = malloc(VERIFY_BLOCK_LIMIT);
    unsigned char *decoded = malloc(VERIFY_BLOCK_LIMIT);
    int result = 1;
    if (length < 0L || (unsigned long)length > VERIFY_PACK_LIMIT || !scratch || !decoded)
        fprintf(stderr, "pack size or host memory unavailable\n");
    else
        result = verify(file, (size_t)length, scratch, decoded, expected);
    free(decoded);
    free(scratch);
    if (expected && fclose(expected) != 0)
        result = 1;
    if (fclose(file) != 0)
        result = 1;
    return result;
}
