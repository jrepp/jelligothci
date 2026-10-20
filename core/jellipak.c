#include "jelli/jellipak.h"

#include <string.h>

static uint32_t load32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint32_t crc_update(uint32_t crc, const unsigned char *data, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = UINT32_C(0) - (crc & UINT32_C(1));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc;
}

uint32_t jelli_jellipak_crc32(const unsigned char *data, size_t size)
{
    return crc_update(UINT32_MAX, data, size) ^ UINT32_MAX;
}

/* JelliPakReadAt also supports mutable file/transport contexts. */
// cppcheck-suppress constParameterPointer
bool jelli_jellipak_memory_read(void *context, size_t offset, unsigned char *out, size_t size)
{
    const JelliPakMemory *memory = context;
    if (memory == NULL || (memory->data == NULL && memory->size != 0U) ||
        (out == NULL && size != 0U) || offset > memory->size || size > memory->size - offset) {
        return false;
    }
    if (size != 0U) {
        memcpy(out, memory->data + offset, size);
    }
    return true;
}

static JelliPakBlock parse_entry(const unsigned char *entry)
{
    const JelliPakBlock block = {.id = load32(entry),
                                 .type = load32(entry + 4),
                                 .codec = load32(entry + 8),
                                 .offset = load32(entry + 12),
                                 .stored_size = load32(entry + 16),
                                 .decoded_size = load32(entry + 20),
                                 .stored_crc32 = load32(entry + 24),
                                 .decoded_crc32 = load32(entry + 28)};
    return block;
}

JelliPakStatus jelli_jellipak_block(const JelliPak *pak, uint32_t index, JelliPakBlock *out)
{
    unsigned char entry[JELLI_PAK_ENTRY_SIZE];
    size_t relative = 0;
    size_t offset = 0;
    if (pak == NULL || out == NULL || pak->source.read_at == NULL || index >= pak->block_count) {
        return JELLI_PAK_INVALID;
    }
    if (jelli_size_multiply(index, JELLI_PAK_ENTRY_SIZE, &relative) != JELLI_MEMORY_OK ||
        jelli_size_add(JELLI_PAK_HEADER_SIZE, relative, &offset) != JELLI_MEMORY_OK ||
        offset > pak->source.size || sizeof(entry) > pak->source.size - offset) {
        return JELLI_PAK_FORMAT;
    }
    if (!pak->source.read_at(pak->source.context, offset, entry, sizeof(entry))) {
        return JELLI_PAK_IO;
    }
    *out = parse_entry(entry);
    return JELLI_PAK_OK;
}

static JelliPakStatus check_header(const unsigned char *header, JelliPak *pak, size_t *payload)
{
    size_t directory_size = 0;
    if (memcmp(header, "JPAK", 4) != 0 || header[6] != JELLI_PAK_HEADER_SIZE || header[7] != 0U ||
        load32(header + 8) != pak->source.size || load32(header + 16) != JELLI_PAK_HEADER_SIZE ||
        load32(header + 20) != JELLI_PAK_ENTRY_SIZE) {
        return JELLI_PAK_FORMAT;
    }
    if (header[4] != 1U || header[5] != 0U) {
        return JELLI_PAK_VERSION;
    }
    if (jelli_jellipak_crc32(header, 28) != load32(header + 28)) {
        return JELLI_PAK_INTEGRITY;
    }
    pak->block_count = load32(header + 12);
    if (pak->block_count > pak->limits.max_blocks) {
        return JELLI_PAK_CAPACITY;
    }
    if (jelli_size_multiply(pak->block_count, JELLI_PAK_ENTRY_SIZE, &directory_size) !=
            JELLI_MEMORY_OK ||
        jelli_size_add(JELLI_PAK_HEADER_SIZE, directory_size, payload) != JELLI_MEMORY_OK ||
        *payload > pak->source.size) {
        return JELLI_PAK_FORMAT;
    }
    return JELLI_PAK_OK;
}

static JelliPakStatus check_block(const JelliPak *pak, JelliPakBlock block, uint32_t last_id,
                                  size_t *payload)
{
    if (block.id == 0U || block.id <= last_id || block.type != JELLI_PAK_BLOCK_TYPE ||
        block.offset != *payload || block.stored_size == 0U || block.decoded_size == 0U ||
        (block.codec == 0U && block.stored_size != block.decoded_size)) {
        return JELLI_PAK_FORMAT;
    }
    if (block.codec > 1U) {
        return JELLI_PAK_CODEC;
    }
    if (block.stored_size > pak->limits.max_stored_block ||
        block.decoded_size > pak->limits.max_decoded_block) {
        return JELLI_PAK_CAPACITY;
    }
    if (jelli_size_add(*payload, block.stored_size, payload) != JELLI_MEMORY_OK ||
        *payload > pak->source.size) {
        return JELLI_PAK_FORMAT;
    }
    return JELLI_PAK_OK;
}

static JelliPakStatus check_directory(const JelliPak *pak, size_t payload, uint32_t expected_crc)
{
    uint32_t crc = UINT32_MAX;
    uint32_t last_id = 0;
    for (uint32_t index = 0; index < pak->block_count; ++index) {
        JelliPakBlock block;
        unsigned char bytes[JELLI_PAK_ENTRY_SIZE];
        const size_t offset = JELLI_PAK_HEADER_SIZE + (size_t)index * JELLI_PAK_ENTRY_SIZE;
        if (!pak->source.read_at(pak->source.context, offset, bytes, sizeof(bytes))) {
            return JELLI_PAK_IO;
        }
        crc = crc_update(crc, bytes, sizeof(bytes));
        block = parse_entry(bytes);
        const JelliPakStatus status = check_block(pak, block, last_id, &payload);
        if (status != JELLI_PAK_OK) {
            return status;
        }
        last_id = block.id;
    }
    if (payload != pak->source.size) {
        return JELLI_PAK_FORMAT;
    }
    return (crc ^ UINT32_MAX) == expected_crc ? JELLI_PAK_OK : JELLI_PAK_INTEGRITY;
}

JelliPakStatus jelli_jellipak_mount(JelliPakSource source, JelliPakLimits limits, JelliPak *out)
{
    unsigned char header[JELLI_PAK_HEADER_SIZE];
    size_t payload = 0;
    if (out == NULL || source.read_at == NULL || limits.max_pack_size == 0U ||
        limits.max_blocks == 0U || limits.max_stored_block == 0U ||
        limits.max_decoded_block == 0U) {
        return JELLI_PAK_INVALID;
    }
    if (source.size > limits.max_pack_size) {
        return JELLI_PAK_CAPACITY;
    }
    if (source.size < sizeof(header) || source.size > UINT32_MAX) {
        return JELLI_PAK_FORMAT;
    }
    if (!source.read_at(source.context, 0, header, sizeof(header))) {
        return JELLI_PAK_IO;
    }
    JelliPak candidate = {.source = source, .limits = limits, .block_count = 0};
    JelliPakStatus status = check_header(header, &candidate, &payload);
    if (status == JELLI_PAK_OK) {
        status = check_directory(&candidate, payload, load32(header + 24));
    }
    if (status == JELLI_PAK_OK) {
        *out = candidate;
    }
    return status;
}
