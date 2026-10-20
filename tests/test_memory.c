#include "jelli/memory.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void checked_arithmetic(void)
{
    size_t result = 17u;
    CHECK(jelli_size_add(SIZE_MAX, 1u, &result) == JELLI_MEMORY_OVERFLOW);
    CHECK(result == 17u);
    CHECK(jelli_size_add(SIZE_MAX, 0u, &result) == JELLI_MEMORY_OK);
    CHECK(result == SIZE_MAX);
    CHECK(jelli_size_add(0u, SIZE_MAX, &result) == JELLI_MEMORY_OK);
    CHECK(result == SIZE_MAX);
    CHECK(jelli_size_add(1u, 2u, NULL) == JELLI_MEMORY_INVALID);
    CHECK(jelli_size_multiply(SIZE_MAX, 2u, &result) == JELLI_MEMORY_OVERFLOW);
    CHECK(result == SIZE_MAX);
    CHECK(jelli_size_multiply(SIZE_MAX / 2u + 1u, 2u, &result) == JELLI_MEMORY_OVERFLOW);
    CHECK(result == SIZE_MAX);
    CHECK(jelli_size_multiply(SIZE_MAX, 1u, &result) == JELLI_MEMORY_OK);
    CHECK(result == SIZE_MAX);
    CHECK(jelli_size_multiply(SIZE_MAX, 0u, &result) == JELLI_MEMORY_OK);
    CHECK(result == 0u);
    CHECK(jelli_size_multiply(0u, SIZE_MAX, &result) == JELLI_MEMORY_OK);
    CHECK(result == 0u);
    CHECK(jelli_size_multiply(1u, 2u, NULL) == JELLI_MEMORY_INVALID);
}

static void checked_alignment(void)
{
    size_t result = 19u;
    CHECK(jelli_size_align(3u, 0u, &result) == JELLI_MEMORY_INVALID);
    CHECK(result == 19u);
    CHECK(jelli_size_align(3u, 3u, &result) == JELLI_MEMORY_INVALID);
    CHECK(result == 19u);
    CHECK(jelli_size_align(3u, _Alignof(max_align_t) * 2u, &result) == JELLI_MEMORY_INVALID);
    CHECK(result == 19u);
    CHECK(jelli_size_align(1u, 1u, NULL) == JELLI_MEMORY_INVALID);
    for (size_t alignment = 1u; alignment <= _Alignof(max_align_t); alignment *= 2u) {
        CHECK(jelli_size_align(0u, alignment, &result) == JELLI_MEMORY_OK);
        CHECK(result == 0u);
        CHECK(jelli_size_align(alignment, alignment, &result) == JELLI_MEMORY_OK);
        CHECK(result == alignment);
        CHECK(jelli_size_align(alignment + 1u, alignment, &result) == JELLI_MEMORY_OK);
        CHECK(result == (alignment == 1u ? 2u : 2u * alignment));
        CHECK(jelli_size_align(SIZE_MAX, alignment, &result) ==
              (alignment == 1u ? JELLI_MEMORY_OK : JELLI_MEMORY_OVERFLOW));
        CHECK(result == (alignment == 1u ? SIZE_MAX : 2u * alignment));
    }
}

static void spans(void)
{
    unsigned char storage[9] = {0};
    JelliByteSpan original = {.data = storage, .capacity = sizeof(storage)};
    JelliByteSpan out = original;
    CHECK(jelli_span_subspan((JelliByteSpan){NULL, 0u}, 0u, 0u, &out) == JELLI_MEMORY_OK);
    CHECK(out.data == NULL && out.capacity == 0u);
    CHECK(jelli_span_subspan((JelliByteSpan){storage, 0u}, 0u, 0u, &out) == JELLI_MEMORY_OK);
    CHECK(out.data == storage && out.capacity == 0u);
    out = original;
    CHECK(jelli_span_subspan((JelliByteSpan){NULL, 1u}, 0u, 0u, &out) == JELLI_MEMORY_INVALID);
    CHECK(out.data == original.data && out.capacity == original.capacity);
    CHECK(jelli_span_subspan(original, 0u, sizeof(storage), &out) == JELLI_MEMORY_OK);
    CHECK(out.data == storage && out.capacity == sizeof(storage));
    CHECK(jelli_span_subspan(original, sizeof(storage), 0u, &out) == JELLI_MEMORY_OK);
    CHECK(out.data == storage + sizeof(storage) && out.capacity == 0u);
    CHECK(jelli_span_subspan(original, 2u, 3u, &out) == JELLI_MEMORY_OK);
    CHECK(out.data == storage + 2u && out.capacity == 3u);
    out.data[0] = 42u;
    CHECK(storage[2] == 42u);
    CHECK(jelli_span_subspan(original, 10u, 0u, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(out.data == storage + 2u && out.capacity == 3u);
    CHECK(jelli_span_subspan(original, 8u, 2u, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(jelli_span_subspan(original, 1u, SIZE_MAX, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(jelli_span_subspan(original, SIZE_MAX, 2u, &out) == JELLI_MEMORY_CAPACITY);
    CHECK(out.data == storage + 2u && out.capacity == 3u);
    CHECK(jelli_span_subspan(original, 0u, 0u, NULL) == JELLI_MEMORY_INVALID);
}

int main(void)
{
    checked_arithmetic();
    checked_alignment();
    spans();
    puts("Memory: checked arithmetic, alignment and span bounds passed.");
    return 0;
}
