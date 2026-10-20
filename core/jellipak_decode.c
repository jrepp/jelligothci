#include "jelli/jellipak.h"

#include <string.h>

/* Independent implementation from the LZ4 block format specification:
 * https://github.com/lz4/lz4/blob/v1.9.4/doc/lz4_Block_format.md
 * Accept safe exact-length blocks without enforcing encoder-only last-match
 * placement restrictions. No external dictionaries or frame format. */
static bool read_length(const unsigned char *input, size_t size, size_t *position, size_t initial,
                        size_t *length)
{
    size_t result = initial;
    if (initial == 15U) {
        unsigned char extension;
        do {
            if (*position == size) {
                return false;
            }
            extension = input[*position];
            ++*position;
            if (jelli_size_add(result, extension, &result) != JELLI_MEMORY_OK) {
                return false;
            }
        } while (extension == 255U);
    }
    *length = result;
    return true;
}

static bool lz4_decode(const unsigned char *input, size_t input_size, unsigned char *output,
                       size_t output_size)
{
    size_t source = 0;
    size_t target = 0;
    while (source < input_size) {
        const unsigned char token = input[source++];
        size_t literals = 0;
        size_t match = 0;
        if (!read_length(input, input_size, &source, (size_t)(token >> 4U), &literals) ||
            literals > input_size - source || literals > output_size - target) {
            return false;
        }
        memcpy(output + target, input + source, literals);
        source += literals;
        target += literals;
        if (source == input_size) {
            return target == output_size;
        }
        if (input_size - source < 2U) {
            return false;
        }
        const size_t distance = (size_t)input[source] | ((size_t)input[source + 1U] << 8U);
        source += 2U;
        if (distance == 0U || distance > target ||
            !read_length(input, input_size, &source, (size_t)(token & 15U), &match) ||
            jelli_size_add(match, 4, &match) != JELLI_MEMORY_OK || match > output_size - target) {
            return false;
        }
        /* A forward byte copy deliberately supports overlapping LZ4 matches. */
        for (size_t index = 0; index < match; ++index) {
            output[target] = output[target - distance];
            ++target;
        }
    }
    return false;
}

JelliPakStatus jelli_jellipak_decode(const JelliPak *pak, uint32_t index,
                                     JelliByteSpan stored_scratch, JelliByteSpan decoded,
                                     size_t *written)
{
    JelliPakBlock block;
    if (written == NULL || (stored_scratch.data == NULL && stored_scratch.capacity != 0U) ||
        (decoded.data == NULL && decoded.capacity != 0U)) {
        return JELLI_PAK_INVALID;
    }
    const JelliPakStatus status = jelli_jellipak_block(pak, index, &block);
    if (status != JELLI_PAK_OK) {
        return status;
    }
    if (block.stored_size > stored_scratch.capacity || block.decoded_size > decoded.capacity) {
        return JELLI_PAK_CAPACITY;
    }
    if (stored_scratch.data == NULL || decoded.data == NULL) {
        return JELLI_PAK_INVALID;
    }
    if (!pak->source.read_at(pak->source.context, block.offset, stored_scratch.data,
                             block.stored_size)) {
        return JELLI_PAK_IO;
    }
    if (jelli_jellipak_crc32(stored_scratch.data, block.stored_size) != block.stored_crc32) {
        return JELLI_PAK_INTEGRITY;
    }
    if (block.codec == 0U) {
        memcpy(decoded.data, stored_scratch.data, block.decoded_size);
    } else if (block.codec != 1U) {
        return JELLI_PAK_CODEC;
    } else if (!lz4_decode(stored_scratch.data, block.stored_size, decoded.data,
                           block.decoded_size)) {
        return JELLI_PAK_DECODE;
    }
    if (jelli_jellipak_crc32(decoded.data, block.decoded_size) != block.decoded_crc32) {
        return JELLI_PAK_INTEGRITY;
    }
    *written = block.decoded_size;
    return JELLI_PAK_OK;
}
