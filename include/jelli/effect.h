#ifndef JELLI_EFFECT_H
#define JELLI_EFFECT_H
#include "jelli/creature.h"

#define JELLI_EFFECT_TRACK_CAPACITY 16u
#define JELLI_EFFECT_CUE_CAPACITY 16u
#define JELLI_EFFECT_POINT_CAPACITY 8u
#define JELLI_EFFECT_DURATION_MAX 30000u

typedef enum {
    JELLI_EFFECT_BEHIND,
    JELLI_EFFECT_FRONT,
    JELLI_EFFECT_FOREGROUND,
    JELLI_EFFECT_LAYER_COUNT
} JelliEffectLayer;

typedef enum {
    JELLI_EFFECT_EVOLUTION,
    JELLI_EFFECT_PRESENT_CAUGHT,
    JELLI_EFFECT_TRIGGER_COUNT
} JelliEffectTrigger;
/* One-based definition indices; zero disables an automatic trigger. */
extern const uint8_t jelli_effect_triggers[JELLI_EFFECT_TRIGGER_COUNT];

typedef struct {
    int16_t x, y;
} JelliEffectPoint;
typedef struct {
    JelliClip clip;
    uint16_t start_ms, duration_ms;
    int16_t x, y, end_x, end_y; /* Native display pixel offsets from attachment. */
    uint8_t layer, anchor, scale;
    bool ease; /* Smoothstep instead of linear. */
} JelliEffectTrack;
typedef struct {
    uint16_t start_ms, duration_ms;
    uint8_t pose;
} JelliEffectCue;
typedef struct {
    const char *key;
    uint16_t duration_ms;
    uint8_t track_count, cue_count;
    JelliEffectTrack tracks[JELLI_EFFECT_TRACK_CAPACITY];
    JelliEffectCue cues[JELLI_EFFECT_CUE_CAPACITY];
} JelliEffectDefinition;
typedef struct {
    uint32_t asset;
    int16_t x, y;
    uint8_t layer, anchor, scale;
} JelliEffectSprite;
typedef struct {
    JelliEffectSprite sprites[JELLI_EFFECT_TRACK_CAPACITY];
    uint16_t cue_elapsed_ms;
    uint8_t count, pose;
    bool active, has_pose;
} JelliEffectSample;

/* Definitions remain immutable for playback. Validation rejects overlapping pose
 * cues; track overlap is intentional, ordered by layer then authored track order.
 * Sample uses absolute elapsed time, [start,end) intervals, and no event backlog.
 * After the duration the sample is empty. NULL/invalid definitions sample empty. */
bool jelli_effect_valid(const JelliEffectDefinition *definition);
void jelli_effect_sample(const JelliEffectDefinition *definition, uint64_t elapsed_ms,
                         JelliEffectSample *sample);
/* Rendered builds generate these tables. Point 0 is screen origin; remaining
 * points are per-form source-pixel offsets from the actor's ground anchor. */
extern const JelliEffectDefinition jelli_effect_definitions[];
extern const unsigned jelli_effect_definition_count;
JelliEffectPoint jelli_effect_point(unsigned form, unsigned anchor);
#endif
