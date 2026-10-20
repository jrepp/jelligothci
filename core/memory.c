#include "jelli/memory.h"

static bool valid_alignment(size_t alignment)
{
    return alignment != 0 && (alignment & (alignment - 1)) == 0 &&
           alignment <= _Alignof(max_align_t);
}

static bool valid_span(JelliByteSpan span) { return span.data != NULL || span.capacity == 0; }

JelliMemoryStatus jelli_size_add(size_t left, size_t right, size_t *out)
{
    if (out == NULL)
        return JELLI_MEMORY_INVALID;
    if (right > SIZE_MAX - left)
        return JELLI_MEMORY_OVERFLOW;
    *out = left + right;
    return JELLI_MEMORY_OK;
}

JelliMemoryStatus jelli_size_multiply(size_t left, size_t right, size_t *out)
{
    if (out == NULL)
        return JELLI_MEMORY_INVALID;
    if (left != 0 && right > SIZE_MAX / left)
        return JELLI_MEMORY_OVERFLOW;
    *out = left * right;
    return JELLI_MEMORY_OK;
}

JelliMemoryStatus jelli_size_align(size_t size, size_t alignment, size_t *out)
{
    if (out == NULL || !valid_alignment(alignment))
        return JELLI_MEMORY_INVALID;
    size_t remainder = size % alignment;
    size_t padding = remainder == 0 ? 0 : alignment - remainder;
    return jelli_size_add(size, padding, out);
}

JelliMemoryStatus jelli_span_subspan(JelliByteSpan span, size_t offset, size_t length,
                                     JelliByteSpan *out)
{
    if (out == NULL || !valid_span(span))
        return JELLI_MEMORY_INVALID;
    if (offset > span.capacity || length > span.capacity - offset)
        return JELLI_MEMORY_CAPACITY;
    JelliByteSpan result = {span.data, length};
    if (span.data != NULL)
        result.data += offset;
    *out = result;
    return JELLI_MEMORY_OK;
}

JelliMemoryStatus jelli_arena_init(JelliArena *arena, JelliByteSpan storage)
{
    if (arena == NULL || !valid_span(storage))
        return JELLI_MEMORY_INVALID;
    if (storage.data != NULL && (uintptr_t)storage.data % _Alignof(max_align_t) != 0)
        return JELLI_MEMORY_INVALID;
    *arena = (JelliArena){.storage = storage, .epoch = 1};
    return JELLI_MEMORY_OK;
}

/* Only initialized arenas may be used. This also rejects damaged cursor state
 * before any arithmetic; it cannot validate caller storage accessibility. */
static bool valid_arena(const JelliArena *arena)
{
    return arena != NULL && valid_span(arena->storage) && arena->epoch != 0 &&
           arena->used <= arena->storage.capacity && arena->high_water >= arena->used &&
           arena->high_water <= arena->storage.capacity;
}

JelliMemoryStatus jelli_arena_allocate(JelliArena *arena, size_t size, size_t alignment,
                                       JelliByteSpan *out)
{
    if (!valid_arena(arena) || out == NULL || size == 0 || !valid_alignment(alignment))
        return JELLI_MEMORY_INVALID;
    if (arena->checked_out)
        return JELLI_MEMORY_BUSY;
    size_t offset;
    JelliMemoryStatus status = jelli_size_align(arena->used, alignment, &offset);
    if (status != JELLI_MEMORY_OK)
        return status;
    size_t end;
    status = jelli_size_add(offset, size, &end);
    if (status != JELLI_MEMORY_OK)
        return status;
    if (end > arena->storage.capacity)
        return JELLI_MEMORY_CAPACITY;
    JelliByteSpan result = {arena->storage.data + offset, size};
    arena->used = end;
    if (end > arena->high_water)
        arena->high_water = end;
    *out = result;
    return JELLI_MEMORY_OK;
}

JelliMemoryStatus jelli_arena_mark(const JelliArena *arena, JelliArenaMark *out)
{
    if (!valid_arena(arena) || out == NULL)
        return JELLI_MEMORY_INVALID;
    if (arena->checked_out)
        return JELLI_MEMORY_BUSY;
    *out = (JelliArenaMark){arena, arena->epoch, arena->used};
    return JELLI_MEMORY_OK;
}

static JelliMemoryStatus move_cursor(JelliArena *arena, size_t offset)
{
    if (arena->epoch == UINT64_MAX)
        return JELLI_MEMORY_EXHAUSTED;
    arena->used = offset;
    ++arena->epoch;
    return JELLI_MEMORY_OK;
}

JelliMemoryStatus jelli_arena_rewind(JelliArena *arena, JelliArenaMark mark)
{
    if (!valid_arena(arena))
        return JELLI_MEMORY_INVALID;
    if (arena->checked_out)
        return JELLI_MEMORY_BUSY;
    if (mark.arena != arena || mark.epoch != arena->epoch || mark.offset > arena->used)
        return JELLI_MEMORY_STALE;
    return move_cursor(arena, mark.offset);
}

JelliMemoryStatus jelli_arena_reset(JelliArena *arena)
{
    if (!valid_arena(arena))
        return JELLI_MEMORY_INVALID;
    if (arena->checked_out)
        return JELLI_MEMORY_BUSY;
    return move_cursor(arena, 0);
}

JelliMemoryStatus jelli_arena_checkout(JelliArena *arena, JelliArenaLease *out)
{
    if (!valid_arena(arena) || out == NULL)
        return JELLI_MEMORY_INVALID;
    if (arena->checked_out)
        return JELLI_MEMORY_BUSY;
    if (arena->lease_generation == UINT64_MAX)
        return JELLI_MEMORY_EXHAUSTED;
    ++arena->lease_generation;
    arena->checked_out = true;
    *out = (JelliArenaLease){arena, arena->lease_generation};
    return JELLI_MEMORY_OK;
}

JelliMemoryStatus jelli_arena_checkin(JelliArena *arena, JelliArenaLease lease)
{
    if (!valid_arena(arena))
        return JELLI_MEMORY_INVALID;
    if (!arena->checked_out || lease.arena != arena || lease.generation != arena->lease_generation)
        return JELLI_MEMORY_STALE;
    arena->checked_out = false;
    return JELLI_MEMORY_OK;
}
