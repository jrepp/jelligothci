#ifndef JELLI_GAME_H
#define JELLI_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define JELLI_PET_CAPACITY 8u
#define JELLI_NEED_COUNT 5u
#define JELLI_STACK_LIMIT 20u
#define JELLI_DAY_TICKS 864000u
#define JELLI_OFFLINE_CAP_MS 21600000u

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
    JELLI_CMD_BEDTIME
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
    uint64_t ticks, stage_ticks, interaction_due, nap_due, awake_until;
    uint64_t wake_override_until, hunger_due;
    uint32_t phase_offset, bedtime, sleep_duration, random_state;
    uint16_t needs[JELLI_NEED_COUNT];
    uint16_t need_remainders[JELLI_NEED_COUNT];
    uint16_t bond, feeds, neglect;
    uint8_t form, location;
    JelliHealth health;
    JelliActivity activity;
    bool asleep, scheduled_sleep, hunger_low, hunger_counted;
    bool reward_pending, reward_claimed;
} JelliPet;

typedef struct {
    JelliPet pets[JELLI_PET_CAPACITY];
    uint64_t ticks, discarded_ms, resume_remaining_ms;
    uint32_t backlog_ms, revision;
    uint16_t food, gifts;
    uint8_t count, active;
    bool resuming;
} JelliGame;

typedef struct {
    JelliCommandKind kind;
    uint32_t actor_id;
    /* ACTIVATE: target stable ID; TRAVEL: location 0/1; BEDTIME: hour 0..23. */
    uint32_t value;
} JelliCommand;

/* Caller-owned state. No clock, allocation, IO, or SDK dependencies. */
void jelli_game_init(JelliGame *game);
bool jelli_game_valid(const JelliGame *game);
JelliResult jelli_game_command(JelliGame *game, JelliCommand command);
/* At most eight 100 ms ticks/call, at most two seconds retained backlog. */
void jelli_game_advance(JelliGame *game, uint64_t elapsed_ms);
void jelli_game_resume_begin(JelliGame *game, uint64_t elapsed_ms);
/* At most eight minute segments/call; true when resume is complete. */
bool jelli_game_resume_step(JelliGame *game);
const char *jelli_game_result_name(JelliResult result);

#endif
