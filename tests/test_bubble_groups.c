#include "jelli/bubble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void connected_shapes(void)
{
    static const unsigned shapes[][4] = {{54,55,48,49}, {54,55,48,42}, {54,55,56,49},
                                         {54,49,44,39}, {5,6,11,12}};
    for (unsigned n = 0; n < 5u; ++n) {
        JelliBubble lab;
        jelli_bubble_init(&lab, JELLI_BUBBLE_MANUAL);
        memset(lab.cells, 0, sizeof(lab.cells));
        lab.level = 2u;
        lab.cells[59] = 6u;
        for (unsigned i = 0; i < 4u; ++i)
            lab.cells[shapes[n][i]] = 5u;
        lab.color = 3u;
        jelli_bubble_hard_drop(&lab);
        CHECK(jelli_bubble_valid(&lab));
        CHECK(lab.score == (n < 3u ? 400u : 0u));
    }
    JelliBubble lab;
    jelli_bubble_init(&lab, JELLI_BUBBLE_MANUAL);
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.cells[54] = lab.cells[55] = lab.cells[48] = 5u;
    lab.color = 3u;
    jelli_bubble_hard_drop(&lab);
    CHECK(lab.score == 0u && jelli_bubble_viruses(&lab) == 3u);
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.level = 2u;
    for (unsigned x = 0u; x < JELLI_BUBBLE_WIDTH; ++x)
        lab.cells[54u + x] = 5u;
    jelli_bubble_hard_drop(&lab);
    CHECK(lab.status == JELLI_BUBBLE_WON && lab.score == 600u);
}

static void randomized_solutions(void)
{
    uint8_t first[JELLI_BUBBLE_CELLS];
    unsigned different = 0u;
    for (uint32_t seed = 0u; seed < 100u; ++seed) {
        JelliBubble lab, same;
        jelli_bubble_init_seeded(&lab, JELLI_BUBBLE_MANUAL, seed);
        jelli_bubble_init_seeded(&same, JELLI_BUBBLE_MANUAL, seed);
        CHECK(jelli_bubble_same(&lab, &same));
        if (!seed) memcpy(first, lab.cells, sizeof(first));
        else different += memcmp(first, lab.cells, sizeof(first)) != 0;
        unsigned columns[3] = {0}, level = JELLI_BUBBLE_LEVELS;
        for (unsigned pair = 0u; pair < 90u && lab.status == JELLI_BUBBLE_RUNNING; ++pair) {
            if (level != lab.level) {
                level = lab.level;
                CHECK(jelli_bubble_viruses(&lab) == (level + 1u) * 3u);
                for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; ++i)
                    if (lab.cells[i] >= 5u) columns[(lab.cells[i] & 3u) - 1u] = i % JELLI_BUBBLE_WIDTH;
            }
            CHECK(jelli_bubble_move(&lab, 0, true));
            unsigned column = columns[lab.color - 1u];
            for (unsigned i = 0u; i < JELLI_BUBBLE_WIDTH && lab.x != column; ++i)
                CHECK(jelli_bubble_move(&lab, lab.x < column ? 1 : -1, false));
            jelli_bubble_hard_drop(&lab);
            CHECK(jelli_bubble_valid(&lab));
        }
        CHECK(lab.status == JELLI_BUBBLE_WON);
    }
    CHECK(different > 90u);
}

static void assistance(void)
{
    JelliBubble lab;
    jelli_bubble_init(&lab, JELLI_BUBBLE_MANUAL);
    memset(lab.cells, 0, sizeof(lab.cells));
    lab.cells[59] = 6u;
    lab.cells[0] = 7u; /* Side danger cell does not block the middle spawn. */
    lab.cursor = JELLI_BUBBLE_PAIRS;
    lab.color = 1u;
    jelli_bubble_hard_drop(&lab);
    CHECK(lab.status == JELLI_BUBBLE_RUNNING && lab.cursor == 1u);
    for (unsigned i = 0u; i < 1000u; ++i) jelli_bubble_tick(&lab);
    CHECK(lab.y == 0u);
    lab.assist = JELLI_BUBBLE_RELAXED;
    lab.ticks = JELLI_BUBBLE_RELAXED_TICKS - 1u;
    CHECK(jelli_bubble_move(&lab, 1, false));
    CHECK(lab.ticks == 0u);
}

int main(void)
{
    assistance();
    connected_shapes();
    randomized_solutions();
    puts("PASS: connected groups and 100 seeded solvable progressive runs");
    return 0;
}
