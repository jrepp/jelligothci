#ifndef JELLI_GAME_H
#define JELLI_GAME_H

#include "jelli/events.h"
#include "jelli/habits.h"
#include "jelli/prizes.h"
#include "jelli/sleep_log.h"
#include <stdbool.h>
#include <stdint.h>

#define JELLI_PET_CAPACITY 9u
#define JELLI_NEED_COUNT 5u
#define JELLI_STACK_LIMIT 20u
#define JELLI_DAY_TICKS 864000u
#define JELLI_OFFLINE_CAP_MS 86400000u

typedef enum {
    JELLI_SATIETY,
    JELLI_ENERGY,
    JELLI_HYGIENE,
    JELLI_AMUSEMENT,
    JELLI_SOCIAL
} JelliNeed;
typedef enum { JELLI_WELL, JELLI_UNWELL, JELLI_RECOVERING } JelliHealth;
typedef enum {
    JELLI_IDLE,
    JELLI_EATING,
    JELLI_PLAYING,
    JELLI_CLEANING,
    JELLI_CARING,
    JELLI_GIVING
} JelliActivity;
typedef enum {
    JELLI_CMD_FEED,
    JELLI_CMD_PLAY,
    JELLI_CMD_CLEAN,
    JELLI_CMD_CARE,
    JELLI_CMD_REST,
    JELLI_CMD_WAKE,
    JELLI_CMD_GIFT,
    JELLI_CMD_CLAIM,
    JELLI_CMD_ACTIVATE,
    JELLI_CMD_TRAVEL,
    JELLI_CMD_BEDTIME,
    JELLI_CMD_MOMENT,
    JELLI_CMD_HEALTH,
    JELLI_CMD_TOUCH
} JelliCommandKind;
typedef enum {
    JELLI_OK,
    JELLI_BUSY,
    JELLI_NO_ITEM,
    JELLI_ASLEEP,
    JELLI_INVALID_TARGET,
    JELLI_FULL,
    JELLI_NOT_READY
} JelliResult;

typedef struct {
    uint32_t id;
    JelliHabits habits;
    JelliPrizeProgress prize_progress;
    uint64_t ticks, stage_ticks, interaction_due, nap_due, awake_until;
    uint64_t wake_override_until, hunger_due;
    uint64_t shot_until, medicine_until;
    uint32_t phase_offset, bedtime, sleep_duration, random_state;
    uint16_t needs[JELLI_NEED_COUNT];
    uint16_t need_remainders[JELLI_NEED_COUNT];
    uint16_t bond, feeds, neglect;
    uint8_t form, location, shot_goal, shot_hits;
    uint8_t collection_entry, reached_forms;
    /* Brief session-only touch memory; care changes still persist. */
    uint16_t touch_load;
    uint8_t reaction, reaction_ticks;
    JelliHealth health;
    JelliActivity activity;
    bool asleep, scheduled_sleep, hunger_low, hunger_counted;
    bool reward_pending, reward_claimed;
} JelliPet;

typedef struct {
    JelliPet pets[JELLI_PET_CAPACITY];
    JelliSleepLog sleep_log;
    JelliPrizes prizes;
    uint64_t ticks, discarded_ms, resume_remaining_ms;
    uint32_t backlog_ms, revision;
    uint16_t food, gifts;
    uint16_t new_pets;
    uint8_t count, active;
    bool resuming;
    /* Optional borrowed sink; owner outlives commands/advance. Not saved. */
    JelliEventLog *events;
    bool clock_known;
    uint16_t clock_minute;
    /* Current host observation; never a saved or invented wall timestamp. */
    uint64_t wall_seconds;
    bool wall_known;
    /* Persisted display offsets, independently of host clock availability. */
    int16_t timezone_minutes, clock_adjust;
} JelliGame;

typedef struct {
    JelliCommandKind kind;
    uint32_t actor_id;
    /* ACTIVATE: stable ID; TRAVEL: 0/1; BEDTIME: 0..23.
     * MOMENT: breakfast/tea/outing/movie 0..3.
     * HEALTH: brush/medicine/shot/wash/stretch/floss/mouthwash/spit/cleanup 0..8. */
    uint32_t value;
} JelliCommand;

/* Caller-owned state. No clock, allocation, IO, or SDK dependencies. */
void jelli_game_init(JelliGame *game);
/* Caller-owned scratch must differ from game. Runs the real validation/dispatch
 * on a copy without events, preferences, or changes to the live game. */
JelliResult jelli_game_check(const JelliGame *game, JelliCommand command, JelliGame *scratch);
unsigned jelli_pet_shot_goal(const JelliPet *pet);
bool jelli_pet_health_ready(const JelliPet *pet, unsigned activity);
unsigned jelli_pet_mood(const JelliPet *pet);
unsigned jelli_pet_favorite(const JelliPet *pet, unsigned minute);
/* Completion triggers are submitted once by the activity/event owner. SLEEP
 * means a qualified >= six-hour rest; GIFT means a real cross-pet collection gift. */
void jelli_prize_complete(JelliGame *game, unsigned trigger);
JelliResult jelli_prize_catch(JelliGame *game);
JelliResult jelli_prize_gift(JelliGame *game, unsigned index);
bool jelli_game_valid(const JelliGame *game);
JelliResult jelli_game_command(JelliGame *game, JelliCommand command);
/* At most eight 100 ms ticks/call, at most two seconds retained backlog. */
void jelli_game_advance(JelliGame *game, uint64_t elapsed_ms);
void jelli_game_resume_begin(JelliGame *game, uint64_t elapsed_ms);
/* At most one minute (600 simulated ticks)/call; true when resume is complete. */
bool jelli_game_resume_step(JelliGame *game);
const char *jelli_game_result_name(JelliResult result);

#endif
