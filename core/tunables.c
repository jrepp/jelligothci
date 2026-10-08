#include "jelli/tunables.h"
#include <stddef.h>
#include <string.h>

static const JelliTunableDefinition definitions[JELLI_TUNE_COUNT] = {
    {"pet.idle_frame_ms", "ms", 900, 100, 5000},
    {"ui.animation_scale_pct", "duration_percent", 300, 25, 1000},
    {"ui.stat_hold_ms", "unscaled_ms", 3300, 500, 30000},
    {"ui.stat_slide_ms", "unscaled_ms", 300, 50, 2000},
    {"fx.burst_count", "particles", 8, 0, 24},
    {"fx.particle_spread_pct", "percent", 100, 25, 150},
    {"scene.night_transition_ms", "ms", 12000, 1000, 60000},
    {"activity.taps", "taps", 3, 1, 6},
    {"activity.brush.min_taps", "taps", 1, 1, 6},
    {"activity.brush.max_taps", "taps", 3, 1, 6},
    {"activity.floss.min_taps", "taps", 1, 1, 6},
    {"activity.floss.max_taps", "taps", 3, 1, 6},
    {"activity.finish.taps", "taps", 1, 1, 6},
    {"audio.coo_enabled", "boolean", 1, 0, 1},
    {"audio.coo_interval_ms", "ms", 30000, 10000, 300000}};
_Static_assert(JELLI_TUNE_COUNT < 32, "Tuning mask exhausted");
_Static_assert(sizeof(JelliTunables) <= 640u, "Tunable storage exceeds budget");

const JelliTunableDefinition *jelli_tunable_definition(JelliTunable key)
{
    return (unsigned)key < JELLI_TUNE_COUNT ? &definitions[key] : NULL;
}
bool jelli_tunable_find(const char *name, JelliTunable *key)
{
    if (!name || !key)
        return false;
    for (unsigned i = 0; i < JELLI_TUNE_COUNT; ++i)
        if (!strcmp(name, definitions[i].name) ||
            (i <= JELLI_TUNE_NIGHT_MS && !strcmp(name, strchr(definitions[i].name, '.') + 1))) {
            *key = (JelliTunable)i;
            return true;
        }
    return false;
}
static bool valid_values(const JelliTuningValues *v)
{
    if (v->mask >> JELLI_TUNE_COUNT)
        return false;
    for (unsigned i = 0; i < JELLI_TUNE_COUNT; ++i)
        if ((v->mask & (1u << i)) &&
            (v->values[i] < definitions[i].minimum || v->values[i] > definitions[i].maximum))
            return false;
    return true;
}
bool jelli_tunables_register(JelliTunables *s, const JelliTuningProfile *profiles, unsigned count)
{
    if (!s || count > JELLI_TUNING_PROFILES || (count && !profiles))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        if (!valid_values(&profiles[i].settings))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (profiles[i].form == profiles[j].form)
                return false;
    }
    s->profiles = profiles;
    s->profile_count = count;
    ++s->revision; /* Unsigned revision rollover is intentional. */
    return true;
}
static const JelliTuningValues *resolved(const JelliTunables *s, uint32_t id, uint8_t form,
                                         JelliTunable key, const char **source)
{
    *source = "default";
    if (!s || (unsigned)key >= JELLI_TUNE_COUNT)
        return NULL;
    uint32_t bit = 1u << (unsigned)key;
    for (unsigned i = 0; id && i < JELLI_TUNING_PETS; ++i)
        if (s->pets[i].pet_id == id && (s->pets[i].settings.mask & bit)) {
            *source = "creature";
            return &s->pets[i].settings;
        }
    if (s->global.mask & bit) {
        *source = "global";
        return &s->global;
    }
    for (unsigned i = 0; id && i < s->profile_count; ++i)
        if (s->profiles[i].form == form && (s->profiles[i].settings.mask & bit)) {
            *source = "profile";
            return &s->profiles[i].settings;
        }
    return NULL;
}
uint32_t jelli_tunable_get(const JelliTunables *s, uint32_t id, uint8_t form, JelliTunable key)
{
    const JelliTunableDefinition *d = jelli_tunable_definition(key);
    if (!d)
        return 0;
    const char *source;
    const JelliTuningValues *v = resolved(s, id, form, key, &source);
    return v ? v->values[key] : d->initial;
}
const char *jelli_tunable_source(const JelliTunables *s, uint32_t id, uint8_t form,
                                 JelliTunable key)
{
    const char *source;
    (void)resolved(s, id, form, key, &source);
    return source;
}
static JelliTuningValues * override(JelliTunables *s, uint32_t id, bool create)
{
    if (!id)
        return &s->global;
    JelliPetTuning *empty = NULL;
    for (unsigned i = 0; i < JELLI_TUNING_PETS; ++i) {
        if (s->pets[i].pet_id == id)
            return &s->pets[i].settings;
        if (!s->pets[i].settings.mask)
            empty = &s->pets[i];
    }
    if (!empty || !create)
        return NULL;
    *empty = (JelliPetTuning){.pet_id = id};
    return &empty->settings;
}
bool jelli_tunable_set(JelliTunables *s, uint32_t id, JelliTunable key, uint32_t value)
{
    const JelliTunableDefinition *d = jelli_tunable_definition(key);
    if (!s || !d || value < d->minimum || value > d->maximum)
        return false;
    JelliTuningValues *v = override(s, id, true);
    if (!v)
        return false;
    v->values[key] = value;
    v->mask |= 1u << (unsigned)key;
    ++s->revision;
    return true;
}
bool jelli_tunable_reset(JelliTunables *s, uint32_t id, JelliTunable key)
{
    if (!s || !jelli_tunable_definition(key))
        return false;
    JelliTuningValues *v = override(s, id, false);
    if (v)
        v->mask &= ~(1u << (unsigned)key);
    ++s->revision;
    return true;
}
