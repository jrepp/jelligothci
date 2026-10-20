#ifndef JELLI_JELLIPAK_H
#define JELLI_JELLIPAK_H

#include "jelli/memory.h"

#define JELLI_PAK_HEADER_SIZE 32U
#define JELLI_PAK_ENTRY_SIZE 32U
#define JELLI_PAK_BLOCK_TYPE UINT32_C(0x4b4f4c42)

typedef enum {
    JELLI_PAK_OK,
    JELLI_PAK_INVALID,
    JELLI_PAK_IO,
    JELLI_PAK_FORMAT,
    JELLI_PAK_VERSION,
    JELLI_PAK_CODEC,
    JELLI_PAK_INTEGRITY,
    JELLI_PAK_CAPACITY,
    JELLI_PAK_DECODE
} JelliPakStatus;

/* Return true only after filling exactly size bytes. Source bytes/context must
 * remain immutable and alive through all calls using a mounted pack. */
typedef bool (*JelliPakReadAt)(void *context, size_t offset, unsigned char *out, size_t size);
typedef struct {
    void *context;
    size_t size;
    JelliPakReadAt read_at;
} JelliPakSource;
typedef struct {
    const unsigned char *data;
    size_t size;
} JelliPakMemory;
typedef struct {
    size_t max_pack_size;
    size_t max_blocks;
    size_t max_stored_block;
    size_t max_decoded_block;
} JelliPakLimits;
typedef struct {
    JelliPakSource source;
    JelliPakLimits limits;
    uint32_t block_count;
} JelliPak;
typedef struct {
    uint32_t id;
    uint32_t type;
    uint32_t codec;
    uint32_t offset;
    uint32_t stored_size;
    uint32_t decoded_size;
    uint32_t stored_crc32;
    uint32_t decoded_crc32;
} JelliPakBlock;

/* Provisional BLOCK container v1, all integers little endian. Header: JPAK,
 * u16 version=1, u16 header size=32, u32 total size, count, directory offset=32,
 * entry size=32, directory CRC32, header CRC32 (first 28 bytes). Entries: eight
 * u32 fields matching JelliPakBlock above. Type is bytes BLOK; IDs increase and
 * are nonzero. Codec 0 is raw, 1 is independent LZ4 block. Positive block lengths;
 * raw sizes equal. Directory and payloads are contiguous in entry order with
 * no gaps, padding or trailing bytes. Empty containers are valid. CRC32 uses
 * reflected IEEE polynomial 0xedb88320, initial/final xor 0xffffffff.
 * No image/palette semantics, authentication or runtime residency are implied.
 * LZ4 accepts safe exact-length sequences without encoder final-match placement
 * restrictions; final literal-only token low bits are ignored per specification.
 * Limits are required nonzero. All APIs require external synchronization.
 * Block/decode require a successfully mounted, unmodified pack.
 * Pack fields are inspectable, not caller mutable. Outputs/state are unchanged
 * on mount/block failure. Decode may overwrite scratch/output on failure but
 * leaves written unchanged; consume output only on success. Every output object,
 * scratch/output span and source backing must be mutually disjoint (including
 * pack and source context). Caller guarantees span capacity is accessible.
 * No memory allocation, borrowed decode history, or retained scratch pointers. */
JelliPakStatus jelli_jellipak_mount(JelliPakSource source, JelliPakLimits limits, JelliPak *out);
JelliPakStatus jelli_jellipak_block(const JelliPak *pak, uint32_t index, JelliPakBlock *out);
JelliPakStatus jelli_jellipak_decode(const JelliPak *pak, uint32_t index,
                                     JelliByteSpan stored_scratch, JelliByteSpan decoded,
                                     size_t *written);
/* NULL data is valid only when size is zero. */
uint32_t jelli_jellipak_crc32(const unsigned char *data, size_t size);
bool jelli_jellipak_memory_read(void *context, size_t offset, unsigned char *out, size_t size);

#endif
