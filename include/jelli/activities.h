#ifndef JELLI_ACTIVITIES_H
#define JELLI_ACTIVITIES_H
#include "jelli/game.h"

/* Generated from content/activities.json by cmake/JelliActivities.cmake (RFC-005). */
#define JELLI_MOMENT_CAPACITY 8u
#define JELLI_MOMENT_NEVER_SUGGESTED 255u

typedef enum { JELLI_MOMENT_FEED, JELLI_MOMENT_PLAY } JelliMomentKind;
typedef enum { JELLI_MOMENT_STAY, JELLI_MOMENT_HOME, JELLI_MOMENT_GARDEN } JelliMomentLocation;

typedef struct {
    const char *name;
    uint8_t kind, location;
    uint8_t suggest_hour; /* First hour of its suggestion window, or NEVER_SUGGESTED. */
    uint16_t gains[JELLI_NEED_COUNT];
    uint32_t icon, prop; /* Asset IDs; prop 0 means none. */
} JelliMoment;

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
#endif
