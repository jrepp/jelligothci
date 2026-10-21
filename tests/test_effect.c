#include "jelli/effect.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    JelliEffectDefinition d = {
        .duration_ms = 1000u,
        .track_count = 1u,
        .cue_count = 2u,
        .tracks =
            {{.clip = {.frames = {1u, 2u}, .durations_ms = {100u, 200u}, .count = 2u, .loop = true},
              .start_ms = 100u,
              .duration_ms = 800u,
              .x = -100,
              .end_x = 100,
              .scale = 2u}},
        .cues = {{0u, 500u, JELLI_POSE_CURIOUS}, {500u, 500u, JELLI_POSE_HAPPY}}};
    JelliEffectSample s;
    CHECK(jelli_effect_valid(&d));
    jelli_effect_sample(&d, 99u, &s);
    CHECK(s.active && s.count == 0u && s.pose == JELLI_POSE_CURIOUS);
    jelli_effect_sample(&d, 100u, &s);
    CHECK(s.count == 1u && s.sprites[0].asset == 1u && s.sprites[0].x == -100);
    jelli_effect_sample(&d, 500u, &s);
    CHECK(s.sprites[0].x == 0 && s.sprites[0].asset == 2u && s.pose == JELLI_POSE_HAPPY &&
          s.cue_elapsed_ms == 0u);
    jelli_effect_sample(&d, 900u, &s);
    CHECK(s.count == 0u && s.active);
    jelli_effect_sample(&d, UINT64_MAX, &s);
    CHECK(!s.active && !s.has_pose && s.count == 0u);
    d.cues[1].start_ms = 499u;
    CHECK(!jelli_effect_valid(&d));
    d.cues[1].start_ms = 500u;
    d.tracks[0].clip.count = 7u;
    CHECK(!jelli_effect_valid(&d));
    jelli_effect_sample(&d, 500u, &s);
    CHECK(!s.active);
    d.tracks[0].clip.count = 2u;
    d.tracks[0].ease = true;
    jelli_effect_sample(&d, 500u, &s);
    CHECK(s.sprites[0].x == 0);
    d.tracks[0].duration_ms = 1000u;
    CHECK(!jelli_effect_valid(&d));
    jelli_effect_sample(NULL, 0u, &s);
    CHECK(!s.active);
    puts("effect timeline passed");
    return 0;
}
