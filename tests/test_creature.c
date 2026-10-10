#include "jelli/creature.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void looping_clip_wraps(void)
{
    const JelliClip clip = {{1u, 2u, 3u}, {100u, 50u, 250u}, 3u, true};
    CHECK(jelli_clip_frame(&clip, 0u) == 0u);
    CHECK(jelli_clip_frame(&clip, 99u) == 0u);
    CHECK(jelli_clip_frame(&clip, 100u) == 1u);
    CHECK(jelli_clip_frame(&clip, 149u) == 1u);
    CHECK(jelli_clip_frame(&clip, 150u) == 2u);
    CHECK(jelli_clip_frame(&clip, 399u) == 2u);
    CHECK(jelli_clip_frame(&clip, 400u) == 0u);
    CHECK(jelli_clip_frame(&clip, UINT64_MAX) < 3u); /* No overflow on long uptimes. */
}

static void one_shot_clip_holds(void)
{
    const JelliClip blink = {{1u, 2u, 3u, 2u, 1u}, {90u, 90u, 90u, 90u, 450u}, 5u, false};
    CHECK(jelli_clip_frame(&blink, 90u) == 1u);
    CHECK(jelli_clip_frame(&blink, 270u) == 3u);
    CHECK(jelli_clip_frame(&blink, 810u) == 4u);
    CHECK(jelli_clip_frame(&blink, 100000u) == 4u);
}

static void degenerate_clips(void)
{
    const JelliClip empty = {{0u}, {0u}, 0u, true};
    const JelliClip silent = {{7u, 8u}, {0u, 0u}, 2u, true};
    const JelliClip oversized = {{1u, 2u, 3u, 4u, 5u, 6u}, {1u, 1u, 1u, 1u, 1u, 1u}, 200u, false};
    CHECK(jelli_clip_frame(NULL, 5u) == 0u);
    CHECK(jelli_clip_frame(&empty, 5u) == 0u);
    CHECK(jelli_clip_frame(&silent, 5u) == 0u);
    CHECK(jelli_clip_frame(&oversized, 100u) == JELLI_CLIP_FRAME_CAPACITY - 1u);
}

int main(void)
{
    looping_clip_wraps();
    one_shot_clip_holds();
    degenerate_clips();
    puts("PASS: creature clips loop, hold, and bound malformed timing");
    return 0;
}
