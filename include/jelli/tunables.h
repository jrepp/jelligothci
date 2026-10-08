#ifndef JELLI_TUNABLES_H
#define JELLI_TUNABLES_H
#include <stdbool.h>
#include <stdint.h>

#define JELLI_TUNING_PETS 8u
#define JELLI_TUNING_PROFILES 16u
typedef enum {
    JELLI_TUNE_IDLE_MS,
    JELLI_TUNE_ANIMATION_SCALE,
    JELLI_TUNE_STAT_HOLD,
    JELLI_TUNE_STAT_SLIDE,
    JELLI_TUNE_BURST_COUNT,
    JELLI_TUNE_SPREAD,
    JELLI_TUNE_NIGHT_MS,
    JELLI_TUNE_ACTIVITY_TAPS,
    JELLI_TUNE_BRUSH_MIN,
    JELLI_TUNE_BRUSH_MAX,
    JELLI_TUNE_FLOSS_MIN,
    JELLI_TUNE_FLOSS_MAX,
    JELLI_TUNE_FINISH_TAPS,
    JELLI_TUNE_COO_ENABLED,
    JELLI_TUNE_COO_MS,
    JELLI_TUNE_COUNT
} JelliTunable;
typedef struct {
    const char *name, *unit;
    uint32_t initial, minimum, maximum;
} JelliTunableDefinition;
typedef struct {
    uint32_t mask, values[JELLI_TUNE_COUNT];
} JelliTuningValues;
typedef struct {
    uint8_t form;
    JelliTuningValues settings;
} JelliTuningProfile;
typedef struct {
    uint32_t pet_id;
    JelliTuningValues settings;
} JelliPetTuning;
typedef struct {
    JelliTuningValues global;
    JelliPetTuning pets[JELLI_TUNING_PETS];
    const JelliTuningProfile *profiles;
    unsigned profile_count;
    uint32_t revision;
} JelliTunables;

/* Zero-initialize. Engine-thread only. Defaults < form profile < global override
 * < stable pet-ID override. Runtime overrides are session-only, never saved.
 * Registered profiles are immutable, caller-owned, and must outlive this store.
 * A new parameter adds one enum/definition and a consumer; no string lookup in drawing. */
const JelliTunableDefinition *jelli_tunable_definition(JelliTunable key);
bool jelli_tunable_find(const char *name, JelliTunable *key);
bool jelli_tunables_register(JelliTunables *store, const JelliTuningProfile *profiles,
                             unsigned count);
uint32_t jelli_tunable_get(const JelliTunables *store, uint32_t pet_id, uint8_t form,
                           JelliTunable key);
const char *jelli_tunable_source(const JelliTunables *store, uint32_t pet_id, uint8_t form,
                                 JelliTunable key);
/* pet_id == 0 selects global overrides. Invalid mutations leave the store unchanged. */
bool jelli_tunable_set(JelliTunables *store, uint32_t pet_id, JelliTunable key, uint32_t value);
bool jelli_tunable_reset(JelliTunables *store, uint32_t pet_id, JelliTunable key);
#endif
