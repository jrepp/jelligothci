#ifndef JELLI_SAVE_CODEC_H
#define JELLI_SAVE_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *bytes;
    size_t size;
    size_t offset;
    bool failed;
} Writer;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
    bool failed;
    uint8_t version;
} Reader;

static inline void put_u8(Writer *writer, uint8_t value)
{
    if (writer->offset >= writer->size) {
        writer->failed = true;
        return;
    }
    writer->bytes[writer->offset++] = value;
}

static inline void put_u16(Writer *writer, uint16_t value)
{
    put_u8(writer, (uint8_t)(value & 0xffu));
    put_u8(writer, (uint8_t)(value >> 8u));
}

static inline void put_u32(Writer *writer, uint32_t value)
{
    put_u16(writer, (uint16_t)(value & 0xffffu));
    put_u16(writer, (uint16_t)(value >> 16u));
}

static inline void put_u64(Writer *writer, uint64_t value)
{
    put_u32(writer, (uint32_t)(value & UINT64_C(0xffffffff)));
    put_u32(writer, (uint32_t)(value >> 32u));
}

static inline uint8_t get_u8(Reader *reader)
{
    if (reader->offset >= reader->size) {
        reader->failed = true;
        return 0u;
    }
    return reader->bytes[reader->offset++];
}

static inline uint16_t get_u16(Reader *reader)
{
    uint16_t low = get_u8(reader);
    uint16_t high = get_u8(reader);
    return (uint16_t)(low | (uint16_t)(high << 8u));
}

static inline uint32_t get_u32(Reader *reader)
{
    uint32_t low = get_u16(reader);
    uint32_t high = get_u16(reader);
    return low | (high << 16u);
}

static inline int16_t get_i16(Reader *reader)
{
    uint16_t raw = get_u16(reader);
    return raw <= INT16_MAX ? (int16_t)raw : (int16_t)((int32_t)raw - 65536);
}

static inline uint64_t get_u64(Reader *reader)
{
    uint64_t low = get_u32(reader);
    uint64_t high = get_u32(reader);
    return low | (high << 32u);
}

#endif
