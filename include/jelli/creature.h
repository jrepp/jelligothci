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

/* Behaviour conditions a profile orders into pose rules. Order matches
 * CREATURE_CONDITIONS in tools/assets/embed_slice.py. */
typedef enum {
    JELLI_WHEN_ASLEEP,
    JELLI_WHEN_WAKE_GROGGY,
    JELLI_WHEN_WAKE_SURPRISED,
    JELLI_WHEN_WAKE_HAPPY,
    JELLI_WHEN_UNWELL, /* Unwell or recovering. */
    JELLI_WHEN_EATING,
    JELLI_WHEN_PLAYING, /* Playing, giving, or exercising. */
    JELLI_WHEN_TOUCH_HAPPY,
    JELLI_WHEN_TOUCH_UPSET,
    JELLI_WHEN_COUNT
} JelliCreatureCondition;

#define JELLI_CLIP_FRAME_CAPACITY 6u
#define JELLI_POSE_RULE_CAPACITY 12u
#define JELLI_IDLE_BEAT_CAPACITY 16u

typedef struct {
    uint8_t when, pose; /* JelliCreatureCondition; base or state pose. */
} JelliPoseRule;

/* Per-form presentation from content/creatures.json. The first matching rule
 * picks the pose; otherwise the idle schedule does, one pose per idle beat.
 * Every quiet_cycle-th schedule (offset by pet ID) keeps curious/content
 * beats plain; zero never quiets. Scales are integer pixel multipliers. */
typedef struct {
    JelliPoseRule rules[JELLI_POSE_RULE_CAPACITY];
    uint8_t rule_count;
    uint8_t idle_beats[JELLI_IDLE_BEAT_CAPACITY];
    uint8_t idle_beat_count, quiet_cycle;
    uint8_t scale, icon_scale, portrait_scale;
} JelliCreatureProfile;

/* Asset IDs and holds in source milliseconds. A looping clip cycles; any other
 * clip plays once and holds its last frame. count is 1..capacity. */
typedef struct {
    uint32_t frames[JELLI_CLIP_FRAME_CAPACITY];
    uint16_t durations_ms[JELLI_CLIP_FRAME_CAPACITY];
    uint8_t count;
    bool loop;
} JelliClip;

/* How a behaviour state (content/behaviors.json) looks: a pose (base or state pose, see
 * jelli_creature_pose_count), an optional caption, and effect/prop asset IDs (0 for none). */
typedef struct {
    uint8_t pose;
    const char *caption;
    uint32_t effect, prop;
} JelliBehaviorLook;

/* Base poses first (JELLI_POSE_COUNT), then assets.json "state_poses". */
extern const unsigned jelli_creature_pose_count;
/* NULL outside the behaviour state table. */
const JelliBehaviorLook *jelli_behavior_look(unsigned state);

/* Generated from assets/slice/assets.json clips and content/pets.json forms by
 * tools/assets/embed_slice.py. NULL for forms or poses outside the table. */
const JelliClip *jelli_creature_clip(unsigned form, unsigned pose);
/* Generated with the clips; forms outside the table use form 0's profile. */
const JelliCreatureProfile *jelli_creature_profile(unsigned form);
/* Idle pose for an idle beat count; deterministic per pet ID. */
unsigned jelli_creature_idle_pose(const JelliCreatureProfile *profile, uint64_t beat,
                                  uint32_t pet_id);
/* Frame index shown elapsed_ms after the clip started. */
unsigned jelli_clip_frame(const JelliClip *clip, uint64_t elapsed_ms);

#endif
