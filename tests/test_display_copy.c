#include "../ports/esp32/main/display_copy.h"
#include <stdio.h>

enum { COUNT = JELLI_WIDTH * JELLI_HEIGHT, GUARD = 16 };
static uint16_t source_pixels[COUNT + 2 * GUARD];
static uint16_t destination_pixels[COUNT + 2 * GUARD];

static bool check_region(JelliRect r)
{
    for (unsigned i = 0; i < COUNT + 2u * GUARD; ++i) {
        source_pixels[i] = (uint16_t)(i * 17u);
        destination_pixels[i] = 0xa55au;
    }
    jelli_display_copy(destination_pixels + GUARD, source_pixels + GUARD, r);
    for (unsigned i = 0; i < COUNT + 2u * GUARD; ++i) {
        bool inside = false;
        if (i >= GUARD && i < COUNT + GUARD) {
            unsigned x = (i - GUARD) % JELLI_WIDTH;
            unsigned y = (i - GUARD) / JELLI_WIDTH;
            inside = x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
        }
        if (source_pixels[i] != (uint16_t)(i * 17u) ||
            destination_pixels[i] != (inside ? source_pixels[i] : 0xa55au))
            return false;
    }
    return true;
}

int main(void)
{
    static const JelliRect regions[] = {{0, 0, 466, 466},   {0, 17, 466, 50}, {0, 465, 466, 1},
                                        {1, 17, 464, 50},   {465, 465, 1, 1}, {0, 0, 1, 466},
                                        {466, 0, 0, 466},   {0, 466, 466, 0}, {0, 0, 0, 0},
                                        {90, 282, 286, 100}};
    for (unsigned i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        if (!check_region(regions[i])) {
            fprintf(stderr, "Display copy damaged pixels in case %u\n", i);
            return 1;
        }
    }
    puts("Display copy: damage bounds, guards, and source_pixels preservation passed");
    return 0;
}
