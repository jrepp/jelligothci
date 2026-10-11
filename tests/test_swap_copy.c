#include "../ports/esp32/main/swap_copy.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
enum { CAPACITY = 46600, GUARD = 16 };
static uint8_t source_storage[CAPACITY + 2 * GUARD];
static uint8_t dest_storage[CAPACITY + 2 * GUARD];

static void check_case(size_t offset, size_t length)
{
    memset(dest_storage, 0xa5, sizeof(dest_storage));
    for (size_t i = 0; i < sizeof(source_storage); ++i)
        source_storage[i] = (uint8_t)((i * 13u + i / 256u) % 256u);
    const uint8_t *input = source_storage + GUARD + offset;
    uint8_t *output = dest_storage + GUARD + offset;
    jelli_swap_copy(output, input, length);
    for (size_t i = 0; i < sizeof(dest_storage); ++i) {
        if (i >= GUARD + offset && i - GUARD - offset < length * 2u) {
            size_t relative = i - GUARD - offset;
            CHECK(dest_storage[i] == input[relative ^ 1u]);
        } else
            CHECK(dest_storage[i] == 0xa5);
        CHECK(source_storage[i] == (uint8_t)((i * 13u + i / 256u) % 256u));
    }
}

int main(void)
{
    const size_t lengths[] = {0, 1, 2, 3, 7, 36, 465, 466, 23299};
    jelli_swap_copy(NULL, NULL, 0);
    for (size_t offset = 0; offset < 16u; ++offset)
        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
            check_case(offset, lengths[i]);
    check_case(0, CAPACITY / 2u);
    puts("PASS: RGB565 swap/copy matches byte reference, preserves source and guards");
    return 0;
}
