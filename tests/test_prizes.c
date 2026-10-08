#include "jelli/game.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void catch_index(JelliGame *game, unsigned index)
{
    CHECK(game->prizes.offered == index + 1u);
    CHECK(!(game->prizes.owned & (1u << index)));
    CHECK(jelli_prize_catch(game) == JELLI_OK);
    CHECK(game->prizes.owned & (1u << index));
    CHECK(game->prizes.discovered & (1u << index));
    CHECK(jelli_prize_catch(game) == JELLI_NOT_READY);
    CHECK(jelli_game_valid(game));
}

static void routine_counts_and_pending_guards(void)
{
    JelliGame game;
    jelli_game_init(&game);
    for (unsigned i = 0u; i < 2u; ++i) {
        jelli_prize_complete(&game, JELLI_PRIZE_BRUSH);
        CHECK(!game.prizes.offered);
    }
    jelli_prize_complete(&game, JELLI_PRIZE_BRUSH);
    CHECK(game.prizes.offered == 2u && game.prizes.offered_pet == 1u);
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    CHECK(game.prizes.offered == 2u); /* A second offer cannot replace the first. */
    catch_index(&game, 1u);
    for (unsigned i = 0u; i < 10u; ++i)
        jelli_prize_complete(&game, JELLI_PRIZE_BRUSH);
    CHECK(game.pets[0].prize_progress.counts[1] == 3u && !game.prizes.offered);
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    catch_index(&game, 5u);
    for (unsigned i = 0u; i < 3u; ++i)
        jelli_prize_complete(&game, JELLI_PRIZE_TEA);
    catch_index(&game, 3u);
}

static void breakfast_requires_distinct_mornings(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.clock_known = true;
    game.clock_minute = 540u;
    game.wall_known = true;
    game.wall_seconds = 86400u + 9u * 3600u;
    jelli_prize_complete(&game, JELLI_PRIZE_BREAKFAST);
    jelli_prize_complete(&game, JELLI_PRIZE_BREAKFAST);
    CHECK(game.pets[0].prize_progress.counts[2] == 1u);
    game.wall_seconds += 86400u;
    game.clock_minute = 720u;
    jelli_prize_complete(&game, JELLI_PRIZE_BREAKFAST);
    CHECK(game.pets[0].prize_progress.counts[2] == 1u);
    game.clock_minute = 540u;
    jelli_prize_complete(&game, JELLI_PRIZE_BREAKFAST);
    CHECK(!game.prizes.offered);
    game.wall_seconds += 86400u;
    jelli_prize_complete(&game, JELLI_PRIZE_BREAKFAST);
    catch_index(&game, 2u);
    JelliGame relative;
    jelli_game_init(&relative);
    for (unsigned day = 0u; day < 3u; ++day) {
        relative.pets[0].ticks = (uint64_t)day * JELLI_DAY_TICKS;
        jelli_prize_complete(&relative, JELLI_PRIZE_BREAKFAST);
    }
    catch_index(&relative, 2u);
}

static void conditional_prizes(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_ENERGY] = 399u;
    jelli_prize_complete(&game, JELLI_PRIZE_MOVIE);
    CHECK(!game.prizes.offered);
    game.pets[0].needs[JELLI_ENERGY] = 400u;
    jelli_prize_complete(&game, JELLI_PRIZE_MOVIE);
    catch_index(&game, 4u);
    jelli_prize_complete(&game, JELLI_PRIZE_CARE);
    jelli_prize_complete(&game, JELLI_PRIZE_TRAVEL);
    CHECK(!game.prizes.offered && game.pets[0].prize_progress.counts[7] == 1u);
    game.pets[0].location = 1u;
    jelli_prize_complete(&game, JELLI_PRIZE_TRAVEL);
    catch_index(&game, 7u);
    CHECK(game.pets[0].prize_progress.counts[7] == 0u);
    for (unsigned i = 0u; i < 30u && !game.prizes.offered; ++i)
        jelli_prize_complete(&game, JELLI_PRIZE_OUTING);
    catch_index(&game, 0u);
    jelli_prize_complete(&game, JELLI_PRIZE_SLEEP); /* Caller qualified the completed rest. */
    catch_index(&game, 6u);
}

static void real_gifting_and_history(void)
{
    JelliGame game;
    JelliEventLog events = {0};
    jelli_game_init(&game);
    game.events = &events;
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    catch_index(&game, 5u);
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_NOT_READY);
    CHECK(jelli_prize_gift(&game, 9u) == JELLI_INVALID_TARGET);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    game.pets[1].asleep = true;
    game.pets[1].nap_due = 100u;
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_ASLEEP);
    game.pets[1].asleep = false;
    game.pets[1].nap_due = 0u;
    game.pets[1].needs[JELLI_SOCIAL] = 950u;
    game.pets[1].bond = 980u;
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_OK);
    CHECK(game.pets[1].needs[JELLI_SOCIAL] == 1000u && game.pets[1].bond == 1000u);
    CHECK(!(game.prizes.owned & (1u << 5)) && (game.prizes.discovered & (1u << 5)));
    CHECK(game.prizes.origin_pet[5] == 1u && game.gifts == 3u);
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_NO_ITEM);
    catch_index(&game, 8u);
    const JelliEvent *event = jelli_events_at(&events, events.count - 2u);
    CHECK(event && event->code == JELLI_CMD_GIFT && event->value == 6u &&
          event->result == JELLI_OK);
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    catch_index(&game, 5u);
    CHECK(game.prizes.origin_pet[5] == 2u); /* Newly earned copy has a new source. */
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_NOT_READY);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 2u, 1u}) == JELLI_OK);
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_OK);
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_NO_ITEM);
    CHECK(game.prizes.discovered & (1u << 5));
    CHECK(jelli_prize_gift(&game, 8u) == JELLI_OK);
    CHECK(!game.prizes.offered); /* Giving the bow cannot mint another bow. */
    CHECK(jelli_prize_gift(&game, 8u) == JELLI_NO_ITEM);
}

static void invalid_state_is_rejected(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.prizes.owned = 512u;
    CHECK(!jelli_game_valid(&game));
    CHECK(jelli_prize_catch(&game) == JELLI_INVALID_TARGET);
    jelli_game_init(&game);
    game.prizes.discovered = 1u;
    game.prizes.origin_pet[0] = 99u;
    CHECK(!jelli_game_valid(&game));
    jelli_game_init(&game);
    game.pets[0].prize_progress.counts[1] = 4u;
    CHECK(!jelli_game_valid(&game));
    CHECK(!jelli_prizes_valid(NULL) && !jelli_prize_progress_valid(NULL));
}

static void all_nine_and_deterministic_outing(void)
{
    JelliGame game;
    jelli_game_init(&game);
    for (unsigned i = 0u; i < 3u; ++i)
        jelli_prize_complete(&game, JELLI_PRIZE_BRUSH);
    catch_index(&game, 1u);
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    catch_index(&game, 5u);
    for (unsigned day = 0u; day < 3u; ++day) {
        game.pets[0].ticks = (uint64_t)day * JELLI_DAY_TICKS;
        jelli_prize_complete(&game, JELLI_PRIZE_BREAKFAST);
    }
    catch_index(&game, 2u);
    for (unsigned i = 0u; i < 3u; ++i)
        jelli_prize_complete(&game, JELLI_PRIZE_TEA);
    catch_index(&game, 3u);
    jelli_prize_complete(&game, JELLI_PRIZE_MOVIE);
    catch_index(&game, 4u);
    game.pets[0].location = 1u;
    game.pets[0].random_state = 2u;
    for (unsigned i = 0u; i < 2u; ++i) {
        jelli_prize_complete(&game, JELLI_PRIZE_OUTING);
        CHECK(!game.prizes.offered);
    }
    jelli_prize_complete(&game, JELLI_PRIZE_OUTING);
    catch_index(&game, 0u);
    jelli_prize_complete(&game, JELLI_PRIZE_CARE);
    jelli_prize_complete(&game, JELLI_PRIZE_TRAVEL);
    catch_index(&game, 7u);
    jelli_prize_complete(&game, JELLI_PRIZE_SLEEP);
    catch_index(&game, 6u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    CHECK(jelli_prize_gift(&game, 5u) == JELLI_OK);
    catch_index(&game, 8u);
    jelli_prize_complete(&game, JELLI_PRIZE_WASH);
    catch_index(&game, 5u);
    CHECK(game.prizes.discovered == JELLI_PRIZE_MASK && game.prizes.owned == JELLI_PRIZE_MASK);
    CHECK(game.pets[0].prize_progress.counts[1] == 3u);
    CHECK(game.pets[1].prize_progress.counts[1] == 0u);
}

int main(void)
{
    routine_counts_and_pending_guards();
    breakfast_requires_distinct_mornings();
    conditional_prizes();
    real_gifting_and_history();
    invalid_state_is_rejected();
    all_nine_and_deterministic_outing();
    puts("PASS: collectible qualifications, pending catch, gifting and source history");
    return 0;
}
