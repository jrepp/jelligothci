#include "jelli/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static bool same_arena(const JelliArena *left, const JelliArena *right)
{
    return left->storage.data == right->storage.data &&
           left->storage.capacity == right->storage.capacity && left->used == right->used &&
           left->high_water == right->high_water && left->epoch == right->epoch &&
           left->lease_generation == right->lease_generation &&
           left->checked_out == right->checked_out;
}

static void initialization(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[64];
    JelliArena arena;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_OK);
    JelliArena before = arena;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){NULL, 1u}) == JELLI_MEMORY_INVALID);
    CHECK(same_arena(&arena, &before));
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes + 1u, 4u}) == JELLI_MEMORY_INVALID);
    CHECK(same_arena(&arena, &before));
    CHECK(jelli_arena_init(NULL, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){NULL, 0u}) == JELLI_MEMORY_OK);
    CHECK(arena.used == 0u && arena.high_water == 0u);
    JelliByteSpan out = {bytes, sizeof(bytes)};
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(out.data == bytes && out.capacity == sizeof(bytes));
    CHECK(jelli_arena_reset(&arena) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_reset(&arena) == JELLI_MEMORY_OK);
    /* Reinitialize only after discarding all views, marks and leases. */
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_OK);
    CHECK(arena.used == 0u && arena.high_water == 0u && !arena.checked_out);
}

static void allocation_bounds(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[6u * _Alignof(JelliMemoryAlignment)];
    memset(bytes, 0xacu, sizeof(bytes));
    const size_t guard = _Alignof(JelliMemoryAlignment);
    const size_t capacity = 4u * guard;
    JelliArena arena;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes + guard, capacity}) == JELLI_MEMORY_OK);
    JelliByteSpan out = {NULL, 0u};
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, &out) == JELLI_MEMORY_OK);
    CHECK(out.data == bytes + guard && out.capacity == 1u);
    out.data[0] = 7u;
    size_t alignment = _Alignof(JelliMemoryAlignment);
    CHECK(jelli_arena_allocate(&arena, alignment, alignment, &out) == JELLI_MEMORY_OK);
    CHECK(out.data == bytes + guard + alignment && out.capacity == alignment);
    CHECK(arena.used == 2u * alignment && arena.high_water == 2u * alignment);
    memset(out.data, 9, out.capacity);
    size_t remaining = capacity - arena.used;
    CHECK(jelli_arena_allocate(&arena, remaining, 1u, &out) == JELLI_MEMORY_OK);
    CHECK(arena.used == capacity && arena.high_water == capacity);
    memset(out.data, 11, out.capacity);
    JelliByteSpan before = out;
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(jelli_arena_allocate(&arena, SIZE_MAX, alignment, &out) == JELLI_MEMORY_OVERFLOW);
    CHECK(arena.used == capacity && arena.high_water == capacity);
    CHECK(out.data == before.data && out.capacity == before.capacity);
    for (size_t i = 0u; i < guard; ++i)
        CHECK(bytes[i] == 0xacu);
    for (size_t i = guard + capacity; i < sizeof(bytes); ++i)
        CHECK(bytes[i] == 0xacu);
}

static void allocation_failures(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[32];
    JelliArena arena;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_OK);
    JelliByteSpan out = {NULL, 19u};
    CHECK(jelli_arena_allocate(&arena, 0u, 1u, &out) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_allocate(&arena, 1u, 0u, &out) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_allocate(&arena, 1u, 3u, &out) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_allocate(&arena, 1u, _Alignof(JelliMemoryAlignment) * 2u, &out) ==
          JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, NULL) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_allocate(NULL, 1u, 1u, &out) == JELLI_MEMORY_INVALID);
    CHECK(arena.used == 0u && arena.high_water == 0u);
    CHECK(out.data == NULL && out.capacity == 19u);
}

static void alignment_padding_exhaustion(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[2u * _Alignof(JelliMemoryAlignment)];
    const size_t alignment = _Alignof(JelliMemoryAlignment);
    JelliArena arena;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, alignment + 1u}) == JELLI_MEMORY_OK);
    JelliByteSpan out;
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, &out) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_allocate(&arena, 2u, alignment, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(arena.used == 1u && arena.high_water == 1u);
    CHECK(out.data == bytes && out.capacity == 1u);
    CHECK(jelli_arena_allocate(&arena, alignment, 1u, &out) == JELLI_MEMORY_OK);
    CHECK(arena.used == alignment + 1u && arena.high_water == alignment + 1u);
}

static void marks_and_rewind(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[32];
    JelliArena arena, other;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_init(&other, (JelliByteSpan){NULL, 0u}) == JELLI_MEMORY_OK);
    JelliArenaMark start, later, foreign;
    JelliByteSpan out;
    CHECK(jelli_arena_mark(&arena, &start) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_allocate(&arena, 8u, 1u, &out) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_mark(&arena, &later) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_mark(&other, &foreign) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_rewind(&arena, foreign) == JELLI_MEMORY_STALE);
    CHECK(arena.used == 8u && arena.high_water == 8u);
    CHECK(jelli_arena_rewind(&arena, start) == JELLI_MEMORY_OK);
    CHECK(arena.used == 0u && arena.high_water == 8u);
    CHECK(jelli_arena_rewind(&arena, later) == JELLI_MEMORY_STALE);
    CHECK(jelli_arena_allocate(&arena, 8u, 1u, &out) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_rewind(&arena, later) == JELLI_MEMORY_STALE);
    CHECK(jelli_arena_rewind(&arena, start) == JELLI_MEMORY_STALE);
    CHECK(arena.used == 8u && arena.high_water == 8u);
    CHECK(jelli_arena_mark(&arena, &later) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_rewind(&arena, later) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_rewind(&arena, later) == JELLI_MEMORY_STALE);
    CHECK(jelli_arena_mark(&arena, &later) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_reset(&arena) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_rewind(&arena, later) == JELLI_MEMORY_STALE);
    CHECK(arena.used == 0u && arena.high_water == 8u);
    CHECK(jelli_arena_mark(NULL, &later) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_mark(&arena, NULL) == JELLI_MEMORY_INVALID);
}

static void exclusive_checkout(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[32];
    JelliArena arena, other;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_init(&other, (JelliByteSpan){NULL, 0u}) == JELLI_MEMORY_OK);
    JelliByteSpan out;
    CHECK(jelli_arena_allocate(&arena, 8u, 1u, &out) == JELLI_MEMORY_OK);
    JelliArenaMark mark;
    CHECK(jelli_arena_mark(&arena, &mark) == JELLI_MEMORY_OK);
    JelliArenaLease lease, foreign;
    CHECK(jelli_arena_checkout(&arena, &lease) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_checkout(&other, &foreign) == JELLI_MEMORY_OK);
    JelliArenaMark before = mark;
    JelliArenaLease previous = lease;
    CHECK(jelli_arena_checkout(&arena, &lease) == JELLI_MEMORY_BUSY);
    CHECK(lease.arena == previous.arena && lease.generation == previous.generation);
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, &out) == JELLI_MEMORY_BUSY);
    CHECK(out.data == bytes && out.capacity == 8u);
    CHECK(jelli_arena_mark(&arena, &mark) == JELLI_MEMORY_BUSY);
    CHECK(mark.arena == before.arena && mark.epoch == before.epoch && mark.offset == before.offset);
    CHECK(jelli_arena_reset(&arena) == JELLI_MEMORY_BUSY);
    CHECK(jelli_arena_rewind(&arena, mark) == JELLI_MEMORY_BUSY);
    CHECK(jelli_arena_checkin(&arena, foreign) == JELLI_MEMORY_STALE);
    CHECK(jelli_arena_checkin(&arena, (JelliArenaLease){NULL, 0u}) == JELLI_MEMORY_STALE);
    CHECK(arena.checked_out && arena.used == 8u && arena.high_water == 8u);
    CHECK(jelli_arena_checkin(&arena, lease) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_checkin(&arena, lease) == JELLI_MEMORY_STALE);
    CHECK(jelli_arena_checkout(&arena, &lease) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_checkin(&arena, previous) == JELLI_MEMORY_STALE);
    CHECK(arena.checked_out);
    CHECK(jelli_arena_checkin(&arena, lease) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_checkin(&other, foreign) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_reset(&arena) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_checkout(NULL, &lease) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_checkout(&arena, NULL) == JELLI_MEMORY_INVALID);
    CHECK(jelli_arena_checkin(NULL, lease) == JELLI_MEMORY_INVALID);
}

static void exhausted_identities(void)
{
    _Alignas(JelliMemoryAlignment) unsigned char bytes[16];
    JelliArena arena;
    CHECK(jelli_arena_init(&arena, (JelliByteSpan){bytes, sizeof(bytes)}) == JELLI_MEMORY_OK);
    JelliByteSpan out;
    CHECK(jelli_arena_allocate(&arena, 1u, 1u, &out) == JELLI_MEMORY_OK);
    /* Fault injection reaches limits without billions of state transitions. */
    arena.epoch = UINT64_MAX;
    JelliArenaMark mark;
    CHECK(jelli_arena_mark(&arena, &mark) == JELLI_MEMORY_OK);
    CHECK(jelli_arena_reset(&arena) == JELLI_MEMORY_EXHAUSTED);
    CHECK(jelli_arena_rewind(&arena, mark) == JELLI_MEMORY_EXHAUSTED);
    CHECK(arena.epoch == UINT64_MAX && arena.used == 1u && arena.high_water == 1u);
    arena.lease_generation = UINT64_MAX;
    JelliArenaLease lease = {NULL, 7u};
    CHECK(jelli_arena_checkout(&arena, &lease) == JELLI_MEMORY_EXHAUSTED);
    CHECK(lease.arena == NULL && lease.generation == 7u);
    CHECK(!arena.checked_out && arena.lease_generation == UINT64_MAX);
}

int main(void)
{
    initialization();
    allocation_bounds();
    allocation_failures();
    alignment_padding_exhaustion();
    marks_and_rewind();
    exclusive_checkout();
    exhausted_identities();
    puts("Arena: bounds, lifetime identities, exclusive ownership and exhaustion passed.");
    return 0;
}
