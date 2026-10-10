#ifndef JELLI_CREATURE_H
#define JELLI_CREATURE_H

#include <stdbool.h>
#include <stdint.h>

/* Runtime poses; order matches CREATURE_POSES in tools/assets/build_slice.py. */
typedef enum {
    JELLI_POSE_IDLE,
    JELLI_POSE_IDLE_ALT,
    JELLI_POSE_CURIOUS,
    JELLI_POSE_CONTENT,
    JELLI_POSE_EATING,
    JELLI_POSE_HAPPY,
    JELLI_POSE_ASLEEP,
    JELLI_POSE_UNWELL,
    JELLI_POSE_COUNT
} JelliCreaturePose;

#define JELLI_CLIP_FRAME_CAPACITY 6u

/* Asset IDs and holds in source milliseconds. A looping clip cycles; any other
 * clip plays once and holds its last frame. count is 1..capacity. */
typedef struct {
    uint32_t frames[JELLI_CLIP_FRAME_CAPACITY];
    uint16_t durations_ms[JELLI_CLIP_FRAME_CAPACITY];
    uint8_t count;
    bool loop;
} JelliClip;

/* Generated from assets/slice/assets.json clips and content/pets.json forms by
 * tools/assets/embed_slice.py. NULL for forms or poses outside the table. */
const JelliClip *jelli_creature_clip(unsigned form, unsigned pose);
/* Frame index shown elapsed_ms after the clip started. */
unsigned jelli_clip_frame(const JelliClip *clip, uint64_t elapsed_ms);

#endif
