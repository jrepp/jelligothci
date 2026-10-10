#ifndef JELLI_ACTIVITIES_H
#define JELLI_ACTIVITIES_H
#include "jelli/game.h"

/* Generated from content/activities.json by cmake/JelliActivities.cmake (RFC-005). */
#define JELLI_MOMENT_CAPACITY 32u
#define JELLI_ACTIVITY_METERS 7u
#define JELLI_ACTIVITY_HYDRATION 5u
#define JELLI_ACTIVITY_BOND 6u
#define JELLI_ACTIVITY_BONUSES 8u
#define JELLI_MOMENT_NEVER_SUGGESTED 255u

typedef enum { JELLI_MOMENT_FEED, JELLI_MOMENT_PLAY } JelliMomentKind;

typedef enum {
    JELLI_ANIM_HOLD,
    JELLI_ANIM_SIP,
    JELLI_ANIM_WATCH,
    JELLI_ANIM_BREATHE,
    JELLI_ANIM_JOG,
    JELLI_ANIM_CAST,
    JELLI_ANIM_DREAM,
    JELLI_ANIM_REST,
    JELLI_ANIM_THINK,
    JELLI_ANIM_LIFT,
    JELLI_ANIM_SKETCH,
    JELLI_ANIM_DIG,
    JELLI_ANIM_KICK,
    JELLI_ANIM_VOLLEY,
    JELLI_ANIM_SWIM,
    JELLI_ANIM_SWING,
    JELLI_ANIM_CATCH,
    JELLI_ANIM_MIX
} JelliActivityAnimation;

typedef struct {
    uint8_t form, percent;
} JelliActivityBonus;

typedef struct {
    const char *name;
    const char *window_hint, *prerequisite_hint;
    uint8_t kind, locations; /* Bitset of catalog location IDs. */
    bool randomize_location;
    uint8_t suggest_hour; /* First hour of its suggestion window, or NEVER_SUGGESTED. */
    uint16_t gains[JELLI_ACTIVITY_METERS], costs[JELLI_ACTIVITY_METERS];
    uint8_t jitter_pct, bonus_count;
    JelliActivityBonus bonuses[JELLI_ACTIVITY_BONUSES];
    uint16_t duration_s, start_minute, end_minute;
    uint32_t forms; /* Zero permits every form; otherwise one bit per stable form ID. */
    uint8_t random_weight,
        requires,
    animation;           /* requires: ID + 1, zero means none. */
    uint32_t icon, prop; /* Asset IDs; prop 0 means none. */
} JelliMoment;

typedef struct {
    uint8_t form;
    uint16_t energy_pct, hydration_pct;
} JelliActivityPetCosts;
extern const JelliActivityPetCosts jelli_activity_pet_costs[];
extern const unsigned jelli_activity_pet_cost_count;
extern const JelliMoment jelli_moments[];
extern const unsigned jelli_moment_count;

/* Favourite moments (content/activities.json "favorites"). A pet uses profile
 * id % jelli_favorite_profile_count; its favourite is the first window whose until_minute
 * is above the pet's minute of day. The strong window doubles as the bonus window. */
typedef struct {
    uint16_t until_minute;
    uint8_t moment;
} JelliFavoriteWindow;
typedef struct {
    JelliFavoriteWindow windows[4];
    uint8_t window_count;
    uint16_t strong_from_minute, strong_until_minute, strong_bonus, bonus;
} JelliFavoriteProfile;
extern const JelliFavoriteProfile jelli_favorite_profiles[];
extern const unsigned jelli_favorite_profile_count;
extern const uint16_t jelli_favorite_bond;

/* The suggested moment whose window holds hour (0..23); windows wrap at midnight. */
unsigned jelli_moment_suggested(unsigned hour);
JelliResult jelli_moment_available(const JelliGame *game, const JelliPet *pet, unsigned id);
uint16_t jelli_activity_day(const JelliGame *game, const JelliPet *pet);
const char *jelli_moment_hint(const JelliGame *game, const JelliPet *pet, unsigned id);
/* Costs are fixed and paid once on acceptance; rewards are paid on completion. */
unsigned jelli_activity_cost(const JelliPet *pet, unsigned index, unsigned base);
unsigned jelli_moment_cost(const JelliPet *pet, unsigned id, unsigned index);
unsigned jelli_moment_location(const JelliPet *pet, unsigned id);
const char *jelli_moment_cost_hint(const JelliPet *pet, unsigned id);
void jelli_moment_charge(JelliGame *game, JelliPet *pet, unsigned id);
void jelli_moment_reward(const JelliGame *game, JelliPet *pet);
unsigned jelli_moment_bonus_percent(const JelliPet *pet, unsigned id);
unsigned jelli_moment_jitter_percent(const JelliPet *pet, unsigned id);
void jelli_moment_complete(const JelliGame *game, JelliPet *pet);
#endif
