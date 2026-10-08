#include "jelli/game.h"
#include "jelli/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void check_unchanged(const JelliGame *before, const JelliGame *after)
{
    static uint8_t a[JELLI_SAVE_CAPACITY], b[JELLI_SAVE_CAPACITY];
    JelliSave first = {.game = *before}, second = {.game = *after};
    size_t size = jelli_save_encode(&first, a, sizeof(a));
    CHECK(size > 0u && jelli_save_encode(&second, b, sizeof(b)) == size);
    CHECK(memcmp(a, b, size) == 0);
}

static void ready(JelliGame *g)
{
    jelli_game_init(g);
    for (unsigned i = 0; i < JELLI_NEED_COUNT; ++i)
        g->pets[0].needs[i] = 500u;
}

static JelliResult command(JelliGame *g, JelliCommandKind kind, uint32_t value)
{
    return jelli_game_command(g, (JelliCommand){kind, g->pets[g->active].id, value});
}

static void finish(JelliGame *g)
{
    for (unsigned i = 0; i < 40u; ++i)
        jelli_game_advance(g, 800u);
}

static void test_timed_effects(void)
{
    static const JelliCommandKind actions[] = {JELLI_CMD_FEED, JELLI_CMD_PLAY, JELLI_CMD_CLEAN,
                                               JELLI_CMD_CARE, JELLI_CMD_GIFT};
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        JelliGame g;
        ready(&g);
        uint16_t food = g.food, gifts = g.gifts, bond = g.pets[0].bond;
        g.pets[0].health = JELLI_UNWELL;
        CHECK(command(&g, actions[i], 0u) == JELLI_OK);
        CHECK(g.pets[0].activity != JELLI_IDLE);
        finish(&g);
        CHECK(g.pets[0].activity == JELLI_IDLE);
        if (actions[i] == JELLI_CMD_FEED) {
            CHECK(g.food == food - 1u && g.pets[0].needs[JELLI_SATIETY] > 700u);
            CHECK(g.pets[0].feeds == 1u && g.pets[0].reward_pending);
        } else if (actions[i] == JELLI_CMD_PLAY) {
            CHECK(g.pets[0].needs[JELLI_AMUSEMENT] > 700u);
            CHECK(g.pets[0].needs[JELLI_ENERGY] < 500u);
        } else if (actions[i] == JELLI_CMD_CLEAN) {
            CHECK(g.pets[0].needs[JELLI_HYGIENE] > 800u);
        } else if (actions[i] == JELLI_CMD_CARE) {
            CHECK(g.pets[0].health == JELLI_WELL);
        } else {
            CHECK(g.gifts == gifts - 1u && g.pets[0].bond == bond + 25u);
        }
    }
}

static void test_instant_effects(void)
{
    JelliGame g;
    ready(&g);
    CHECK(command(&g, JELLI_CMD_REST, 0u) == JELLI_OK && g.pets[0].asleep);
    CHECK(g.pets[0].nap_due > g.pets[0].ticks);
    CHECK(command(&g, JELLI_CMD_WAKE, 0u) == JELLI_OK && !g.pets[0].asleep);
    CHECK(g.pets[0].awake_until > g.pets[0].ticks);
    CHECK(command(&g, JELLI_CMD_TRAVEL, 1u) == JELLI_OK && g.pets[0].location == 1u);
    CHECK(command(&g, JELLI_CMD_TRAVEL, 1u) == JELLI_NOT_READY);
    uint32_t bedtime = (g.pets[0].bedtime + 1u) % 24u;
    CHECK(command(&g, JELLI_CMD_BEDTIME, bedtime) == JELLI_OK);
    CHECK(g.pets[0].bedtime == bedtime);
    CHECK(command(&g, JELLI_CMD_BEDTIME, bedtime) == JELLI_NOT_READY);
    g.pets[0].reward_pending = true;
    uint16_t food = g.food;
    CHECK(command(&g, JELLI_CMD_CLAIM, 0u) == JELLI_OK);
    CHECK(g.food == food + 3u && g.pets[0].reward_claimed && !g.pets[0].reward_pending);
    CHECK(command(&g, JELLI_CMD_CLAIM, 0u) == JELLI_NOT_READY);
    CHECK(command(&g, JELLI_CMD_ACTIVATE, g.pets[1].id) == JELLI_OK && g.active == 1u);
    CHECK(command(&g, JELLI_CMD_ACTIVATE, g.pets[1].id) == JELLI_NOT_READY);
}

static void test_moments(void)
{
    for (uint32_t i = 0; i < 4u; ++i) {
        JelliGame g;
        ready(&g);
        if (i == 2u)
            g.pets[0].location = 1u; /* An outing still matters in the garden. */
        CHECK(command(&g, JELLI_CMD_MOMENT, i) == JELLI_OK);
        CHECK(g.pets[0].activity == (i ? JELLI_PLAYING : JELLI_EATING));
        finish(&g);
        CHECK(g.pets[0].needs[i ? JELLI_AMUSEMENT : JELLI_SATIETY] > 700u);
        if (i == 1u)
            CHECK(g.pets[0].needs[JELLI_ENERGY] > 500u && g.pets[0].needs[JELLI_SOCIAL] > 500u);
        if (i == 3u)
            CHECK(g.pets[0].needs[JELLI_SOCIAL] > 550u);
    }
}

static void test_health_clicks(void)
{
    for (uint32_t i = 0; i < 9u; ++i) {
        JelliGame g;
        ready(&g);
        if (i == 1u || i == 2u)
            g.pets[0].health = JELLI_UNWELL;
        uint16_t bond = g.pets[0].bond;
        unsigned taps = i == 1u ? 1u : i == 2u ? jelli_pet_shot_goal(&g.pets[0]) : 5u;
        for (unsigned tap = 0; tap < taps; ++tap) {
            CHECK(command(&g, JELLI_CMD_HEALTH, i) == JELLI_OK);
            CHECK(g.pets[0].bond == bond + 5u * (tap + 1u));
        }
        JelliNeed need = i == 4u                ? JELLI_ENERGY
                         : (i == 1u || i == 2u) ? JELLI_SOCIAL
                                                : JELLI_HYGIENE;
        CHECK(g.pets[0].needs[need] >= (i == 1u ? 510u : i == 2u ? 540u : i >= 5u ? 600u : 700u));
        if (i == 1u || i == 2u) {
            CHECK(g.pets[0].health == JELLI_RECOVERING);
            finish(&g);
            CHECK(g.pets[0].health == JELLI_WELL);
        }
        g.pets[0].needs[need] = 1000u;
        g.pets[0].bond = 1000u;
        JelliGame before = g;
        CHECK(command(&g, JELLI_CMD_HEALTH, i) ==
              ((i == 1u || i == 2u) ? JELLI_NOT_READY : JELLI_FULL));
        check_unchanged(&before, &g);
    }
}

static void test_rejections(void)
{
    JelliGame g;
    ready(&g);
    CHECK(command(&g, JELLI_CMD_REST, 0u) == JELLI_OK);
    JelliGame before = g;
    for (uint32_t i = 0; i < 5u; ++i)
        CHECK(command(&g, JELLI_CMD_HEALTH, i) == JELLI_ASLEEP);
    for (uint32_t i = 0; i < 4u; ++i)
        CHECK(command(&g, JELLI_CMD_MOMENT, i) == JELLI_ASLEEP);
    check_unchanged(&before, &g);
    CHECK(command(&g, JELLI_CMD_WAKE, 0u) == JELLI_OK);
    CHECK(command(&g, JELLI_CMD_FEED, 0u) == JELLI_OK);
    before = g;
    CHECK(command(&g, JELLI_CMD_HEALTH, 0u) == JELLI_BUSY);
    CHECK(command(&g, JELLI_CMD_MOMENT, 3u) == JELLI_BUSY);
    CHECK(command(&g, JELLI_CMD_HEALTH, 9u) == JELLI_INVALID_TARGET);
    CHECK(command(&g, JELLI_CMD_MOMENT, 4u) == JELLI_INVALID_TARGET);
    check_unchanged(&before, &g);
}

static void test_touch_and_events(void)
{
    JelliGame g;
    ready(&g);
    JelliEventLog log = {0};
    g.events = &log;
    uint16_t social = g.pets[0].needs[JELLI_SOCIAL];
    CHECK(command(&g, JELLI_CMD_TOUCH, 0u) == JELLI_OK);
    CHECK(g.pets[0].reaction == 1u && g.pets[0].needs[JELLI_SOCIAL] > social);
    for (unsigned i = 0; i < 4u; ++i)
        CHECK(command(&g, JELLI_CMD_TOUCH, 0u) == JELLI_OK);
    CHECK(g.pets[0].reaction == 3u && g.pets[0].touch_load == 1000u);
    CHECK(log.count == 5u && jelli_events_at(&log, 0u)->code == JELLI_CMD_TOUCH);
    CHECK(jelli_events_at(&log, 4u)->after.flags >> 3 == 3u);
    for (unsigned i = 0; i < 20u; ++i)
        jelli_game_advance(&g, 800u);
    CHECK(g.pets[0].touch_load == 0u && g.pets[0].reaction == 0u);
    CHECK(log.count == 5u); /* Routine decay does not flood the event history. */
    CHECK(command(&g, JELLI_CMD_TOUCH, 0u) == JELLI_OK && g.pets[0].reaction == 1u);
    for (unsigned i = 0; i < 40u; ++i)
        CHECK(command(&g, JELLI_CMD_TOUCH, 0u) == JELLI_OK);
    CHECK(log.count == 32u && jelli_events_at(&log, 0u)->sequence == log.sequence - 31u);
    log.sequence = UINT32_MAX;
    CHECK(command(&g, JELLI_CMD_TOUCH, 0u) == JELLI_OK);
    CHECK(log.count == 1u && log.sequence == 1u);
}

static void test_preferences(void)
{
    JelliGame favorite, ordinary;
    ready(&favorite);
    ready(&ordinary);
    favorite.clock_known = ordinary.clock_known = true;
    favorite.clock_minute = 540u;
    ordinary.clock_minute = 1200u;
    CHECK(jelli_pet_favorite(&favorite.pets[0], 540u) == 0u);
    CHECK(jelli_pet_favorite(&favorite.pets[1], 1200u) == 3u);
    CHECK(command(&favorite, JELLI_CMD_MOMENT, 0u) == JELLI_OK);
    CHECK(command(&ordinary, JELLI_CMD_MOMENT, 0u) == JELLI_OK);
    CHECK(favorite.pets[0].bond > ordinary.pets[0].bond);
    CHECK(jelli_pet_mood(&favorite.pets[0]) > jelli_pet_mood(&ordinary.pets[0]));
    favorite.pets[0].form = 1u;
    CHECK(jelli_pet_favorite(&favorite.pets[0], 540u) == 0u);
}

int main(void)
{
    test_touch_and_events();
    test_preferences();
    test_timed_effects();
    test_instant_effects();
    test_moments();
    test_health_clicks();
    test_rejections();
    puts("All game commands have verified internal effects and guarded no-op paths.");
    return 0;
}
