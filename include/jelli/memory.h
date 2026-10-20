#ifndef JELLI_MEMORY_H
#define JELLI_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JELLI_MEMORY_OK,
    JELLI_MEMORY_INVALID,
    JELLI_MEMORY_OVERFLOW,
    JELLI_MEMORY_CAPACITY,
    JELLI_MEMORY_BUSY,
    JELLI_MEMORY_STALE,
    JELLI_MEMORY_EXHAUSTED
} JelliMemoryStatus;

/* Borrowed mutable bytes, never freed by these APIs. NULL is valid iff capacity
 * is zero. The caller guarantees the entire capacity is accessible. Views may
 * overlap; no API copies bytes. Storage must outlive every view. */
typedef struct {
    unsigned char *data;
    size_t capacity;
} JelliByteSpan;

/* Every failure leaves outputs and arena state unchanged. Output objects must
 * not overlap inputs, arena state, or backing storage. Supported alignments are
 * powers of two no greater than _Alignof(max_align_t). */
JelliMemoryStatus jelli_size_add(size_t left, size_t right, size_t *out);
JelliMemoryStatus jelli_size_multiply(size_t left, size_t right, size_t *out);
JelliMemoryStatus jelli_size_align(size_t size, size_t alignment, size_t *out);
JelliMemoryStatus jelli_span_subspan(JelliByteSpan span, size_t offset, size_t length,
                                     JelliByteSpan *out);

/* Public fields support caller-owned storage and inspection, not mutation.
 * All calls require external synchronization, including checkout/checkin.
 * Initialize once at a stable address. Reinitialization/copying is forbidden
 * until all marks, leases and views have been discarded. */
typedef struct {
    JelliByteSpan storage;
    size_t used;
    size_t high_water;
    uint64_t epoch;
    uint64_t lease_generation;
    bool checked_out;
} JelliArena;

typedef struct {
    const JelliArena *arena;
    uint64_t epoch;
    size_t offset;
} JelliArenaMark;

typedef struct {
    const JelliArena *arena;
    uint64_t generation;
} JelliArenaLease;

/* Any nonnull storage base must be aligned to _Alignof(max_align_t), including
 * zero-capacity storage. Empty NULL storage is accepted. The arena object must
 * be disjoint from its backing storage. Initialization does not read the old
 * arena; discard every prior view, mark and lease before reinitializing. */
JelliMemoryStatus jelli_arena_init(JelliArena *arena, JelliByteSpan storage);
/* Zero-byte allocations are invalid. Successful allocations include padding in
 * used/high_water, return exactly size bytes, and do not initialize them. */
JelliMemoryStatus jelli_arena_allocate(JelliArena *arena, size_t size, size_t alignment,
                                       JelliByteSpan *out);
JelliMemoryStatus jelli_arena_mark(const JelliArena *arena, JelliArenaMark *out);
/* Reset/rewind require all reclaimed views to be dead. Both invalidate ALL
 * existing marks, even for a no-op rewind, preventing offset reuse (ABA).
 * Epoch exhaustion rejects the operation. High-water remains historical.
 * Marks must originate from mark(), never be forged or modified. */
JelliMemoryStatus jelli_arena_rewind(JelliArena *arena, JelliArenaMark mark);
JelliMemoryStatus jelli_arena_reset(JelliArena *arena);
/* Checkout freezes allocation, marking, rewind and reset until matching checkin.
 * Transfer previously allocated views to one worker under host synchronization;
 * checkin only after it stops accessing them. The lease is an ownership guard,
 * not a lock or concurrent allocator. Generation exhaustion rejects checkout. */
JelliMemoryStatus jelli_arena_checkout(JelliArena *arena, JelliArenaLease *out);
JelliMemoryStatus jelli_arena_checkin(JelliArena *arena, JelliArenaLease lease);

#endif
