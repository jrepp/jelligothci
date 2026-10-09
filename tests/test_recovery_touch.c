#include "jelli/game.h"
#include "jelli/save.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint32_t rng = 0x2545f491u;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static JelliResult touch(JelliGame *game)
{
    return jelli_game_command(game,
                              (JelliCommand){JELLI_CMD_TOUCH, game->pets[game->active].id, 0u});
}

/* A recovering pet: health RECOVERING requires the CARING activity (game.c). */
static void recovering(JelliGame *game)
{
    jelli_game_init(game);
    JelliPet *pet = &game->pets[game->active];
    pet->health = JELLI_RECOVERING;
    pet->activity = JELLI_CARING;
    pet->interaction_due = 300u;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (pet->needs[i] < 400u)
            pet->needs[i] = 400u;
        pet->need_remainders[i] = 0u;
    }
    CHECK(jelli_game_valid(game));
}

static void check_recovery_floor(const JelliGame *game)
{
    const JelliPet *pet = &game->pets[game->active];
    CHECK(jelli_game_valid(game));
    if (pet->health != JELLI_RECOVERING)
        return;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        CHECK(pet->needs[i] >= 400u);
        CHECK(pet->needs[i] > 400u || pet->need_remainders[i] == 0u);
    }
}

/* The reported case: an overstimulated touch clamps social to the floor. */
static void clamp_to_floor_stays_valid(void)
{
    JelliGame game;
    recovering(&game);
    JelliPet *pet = &game.pets[game.active];
    pet->needs[JELLI_SOCIAL] = 405u;
    pet->need_remainders[JELLI_SOCIAL] = 144u;
    pet->touch_load = 670u;
    CHECK(jelli_game_valid(&game));
    CHECK(touch(&game) == JELLI_OK);
    CHECK(pet->reaction >= 2u && pet->needs[JELLI_SOCIAL] == 400u);
    CHECK(pet->need_remainders[JELLI_SOCIAL] == 0u);
    check_recovery_floor(&game);
    /* The game keeps accepting commands instead of failing every one. */
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, pet->id, 0u}) !=
          JELLI_INVALID_TARGET);
}

/* Landing exactly on 400 (420 - 20) without clamping also clears the remainder. */
static void exact_floor_stays_valid(void)
{
    JelliGame game;
    recovering(&game);
    JelliPet *pet = &game.pets[game.active];
    pet->needs[JELLI_AMUSEMENT] = 420u;
    pet->need_remainders[JELLI_AMUSEMENT] = 599u;
    pet->touch_load = 400u;
    CHECK(touch(&game) == JELLI_OK);
    CHECK(pet->reaction == 2u && pet->needs[JELLI_AMUSEMENT] == 400u);
    CHECK(pet->need_remainders[JELLI_AMUSEMENT] == 0u);
    check_recovery_floor(&game);
}

/* Remainders above the floor are untouched: only resting on the floor clears them. */
static void remainder_above_floor_is_kept(void)
{
    JelliGame game;
    recovering(&game);
    JelliPet *pet = &game.pets[game.active];
    pet->needs[JELLI_SOCIAL] = 700u;
    pet->need_remainders[JELLI_SOCIAL] = 321u;
    pet->touch_load = 700u;
    CHECK(touch(&game) == JELLI_OK);
    CHECK(pet->needs[JELLI_SOCIAL] == 680u && pet->need_remainders[JELLI_SOCIAL] == 321u);
    check_recovery_floor(&game);
}

/* Random recovering pets, touched and advanced in any order, never become invalid. */
static void random_touch_sequences_stay_valid(void)
{
    for (unsigned seed = 0u; seed < 2000u; ++seed) {
        JelliGame game;
        recovering(&game);
        JelliPet *pet = &game.pets[game.active];
        for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
            pet->needs[i] = (uint16_t)(400u + next() % 601u);
            pet->need_remainders[i] = pet->needs[i] == 400u ? 0u : (uint16_t)(next() % 600u);
        }
        pet->touch_load = (uint16_t)(next() % 1001u);
        CHECK(jelli_game_valid(&game));
        for (unsigned step = 0u; step < 40u; ++step) {
            if (next() % 3u)
                (void)touch(&game);
            else
                jelli_game_advance(&game, next() % 4000u);
            check_recovery_floor(&game);
        }
    }
}

static uint16_t reference_adjusted(uint16_t value, int delta, unsigned floor)
{
    int result = (int)value + delta;
    if (result < (int)floor)
        return (uint16_t)floor;
    return result > 1000 ? 1000u : (uint16_t)result;
}

/* The touch rule before the fix, kept as an independent reference. */
static void reference_touch(JelliPet *pet)
{
    if (pet->asleep)
        return;
    pet->touch_load = reference_adjusted(pet->touch_load, 220, 0u);
    pet->reaction = pet->touch_load >= 900u ? 3u : pet->touch_load >= 600u ? 2u : 1u;
    pet->reaction_ticks = 30u;
    unsigned floor = pet->health == JELLI_RECOVERING ? 400u : 0u;
    int change = pet->reaction == 1u ? (int)jelli_habits_social_gain(&pet->habits, 15u) : -20;
    pet->needs[JELLI_SOCIAL] = reference_adjusted(pet->needs[JELLI_SOCIAL], change, floor);
    pet->needs[JELLI_AMUSEMENT] = reference_adjusted(pet->needs[JELLI_AMUSEMENT], change, floor);
    if (pet->reaction == 1u)
        pet->bond = reference_adjusted(pet->bond, 3, 0u);
}

static uint16_t edge_need(void)
{
    static const uint16_t edges[] = {0u,   1u,   19u,  20u,  21u,  380u, 399u, 400u, 401u,
                                     405u, 419u, 420u, 421u, 985u, 986u, 999u, 1000u};
    return next() % 2u ? edges[next() % (sizeof(edges) / sizeof(edges[0]))]
                       : (uint16_t)(next() % 1001u);
}

/* A valid healthy, unwell, sleeping, or recovering pet with varied needs, habits, and load. */
static void random_pet(JelliGame *game, unsigned kind)
{
    if (kind == 3u) {
        recovering(game);
    } else {
        jelli_game_init(game);
        game->pets[0].health = kind == 1u ? JELLI_UNWELL : JELLI_WELL;
        game->pets[0].asleep = game->pets[0].scheduled_sleep = kind == 2u;
    }
    JelliPet *pet = &game->pets[game->active];
    uint64_t lived = next() % (4u * JELLI_HABIT_DAY_TICKS);
    jelli_habits_advance(&pet->habits, 0u, lived, next() % 2u, false);
    pet->ticks = lived;
    if (kind == 3u)
        pet->interaction_due = lived + 300u;
    pet->touch_load = (uint16_t)(next() % 3u ? next() % 1001u : 380u + next() % 300u);
    pet->bond = (uint16_t)(next() % 1001u);
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        uint16_t need = edge_need();
        pet->needs[i] = kind == 3u && need < 400u ? 400u : need;
        pet->need_remainders[i] =
            kind == 3u && pet->needs[i] == 400u ? 0u : (uint16_t)(next() % 600u);
    }
    CHECK(jelli_game_valid(game));
}

/* Pets compared field by field, never by padding: saved fields through the codec (which
 * also requires a valid game), then the session-only touch memory. */
static bool same_pet(const JelliGame *game, const JelliPet *want)
{
    static JelliSave saves[2];
    static uint8_t bytes[2][JELLI_SAVE_CAPACITY];
    const JelliPet *got = &game->pets[game->active];
    saves[0] = (JelliSave){.game = *game};
    saves[1] = (JelliSave){.game = *game};
    saves[1].game.pets[game->active] = *want;
    size_t size = jelli_save_encode(&saves[0], bytes[0], sizeof(bytes[0]));
    return size > 0u && jelli_save_encode(&saves[1], bytes[1], sizeof(bytes[1])) == size &&
           memcmp(bytes[0], bytes[1], size) == 0 && got->touch_load == want->touch_load &&
           got->reaction == want->reaction && got->reaction_ticks == want->reaction_ticks;
}

/* Healthy, unwell, and sleeping pets touch exactly as before the fix; a recovering pet
 * differs only by dropping the remainder of a need left resting on the floor. */
static void touch_matches_reference(void)
{
    unsigned floored = 0u, kept = 0u;
    for (unsigned i = 0u; i < 8000u; ++i) {
        JelliGame game;
        random_pet(&game, i % 4u);
        JelliPet expected;
        memcpy(&expected, &game.pets[game.active], sizeof(expected));
        reference_touch(&expected);
        static const JelliNeed touched[] = {JELLI_SOCIAL, JELLI_AMUSEMENT};
        for (unsigned k = 0u; k < 2u && expected.health == JELLI_RECOVERING; ++k) {
            if (expected.needs[touched[k]] == 400u) {
                floored += expected.need_remainders[touched[k]] != 0u;
                expected.need_remainders[touched[k]] = 0u;
            } else {
                kept += expected.need_remainders[touched[k]] != 0u;
            }
        }
        JelliResult result = touch(&game);
        CHECK(result == (expected.asleep ? JELLI_ASLEEP : JELLI_OK));
        CHECK(same_pet(&game, &expected));
    }
    /* Both sides of the change were exercised, not just the unchanged path. */
    CHECK(floored > 100u && kept > 100u);
}

int main(void)
{
    clamp_to_floor_stays_valid();
    exact_floor_stays_valid();
    remainder_above_floor_is_kept();
    random_touch_sequences_stay_valid();
    touch_matches_reference();
    puts("Recovery touch checks passed");
    return 0;
}
