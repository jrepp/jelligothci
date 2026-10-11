#include "jelli/game.h"
#include "jelli/save.h"
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
static uint8_t checkpoint[JELLI_SAVE_CAPACITY];
static uint8_t comparison[JELLI_SAVE_CAPACITY];

static unsigned finish(JelliGame *game)
{
    unsigned steps = 0;
    while (game->resuming) {
        uint64_t before = game->resume_remaining_ms;
        CHECK(++steps <= JELLI_OFFLINE_CAP_MS / 60000u);
        (void)jelli_game_resume_step(game);
        CHECK(before - game->resume_remaining_ms <= 60000u);
        CHECK(jelli_game_valid(game));
    }
    return steps;
}

static JelliSave sleeping_checkpoint(void)
{
    JelliSave save = {.sequence = 7u, .anchor_valid = true, .anchor_ms = UINT64_C(1728082800000)};
    jelli_game_init(&save.game);
    save.game.wall_known = true;
    save.game.wall_seconds = save.anchor_ms / 1000u;
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_REST, save.game.pets[0].id, 0}) ==
          JELLI_OK);
    CHECK(save.game.sleep_log.active);
    return save;
}

static void overnight_interruption_and_restart(void)
{
    JelliSave saved = sleeping_checkpoint();
    size_t size = jelli_save_encode(&saved, checkpoint, sizeof(checkpoint));
    CHECK(size > 0);
    JelliSave interrupted;
    CHECK(jelli_save_decode(&interrupted, checkpoint, size));
    jelli_game_resume_begin(&interrupted.game, UINT64_C(8) * 3600000u);
    for (unsigned i = 0; i < 17u; ++i)
        CHECK(!jelli_game_resume_step(&interrupted.game));
    CHECK(jelli_save_encode(&interrupted, comparison, sizeof(comparison)) == 0u);
    /* A reset reloads the last committed anchor, not partially advanced RAM. */
    JelliSave restarted;
    CHECK(jelli_save_decode(&restarted, checkpoint, size));
    jelli_game_resume_begin(&restarted.game, UINT64_C(8) * 3600000u);
    CHECK(finish(&restarted.game) == 480u);
    (void)finish(&interrupted.game);
    size_t full_size = jelli_save_encode(&interrupted, checkpoint, sizeof(checkpoint));
    CHECK(full_size > 0u &&
          jelli_save_encode(&restarted, comparison, sizeof(comparison)) == full_size);
    CHECK(memcmp(checkpoint, comparison, full_size) == 0);
    restarted.anchor_ms += UINT64_C(8) * 3600000u;
    restarted.game.wall_seconds = restarted.anchor_ms / 1000u;
    CHECK(restarted.game.sleep_log.active && restarted.game.pets[0].asleep);
    CHECK(jelli_game_command(&restarted.game,
                             (JelliCommand){JELLI_CMD_WAKE, restarted.game.pets[0].id, 0}) ==
          JELLI_OK);
    CHECK(restarted.game.sleep_log.count == 1u);
    CHECK(restarted.game.sleep_log.sessions[0].duration_seconds == 28800u);
    size = jelli_save_encode(&restarted, checkpoint, sizeof(checkpoint));
    CHECK(size > 0u && jelli_save_decode(&saved, checkpoint, size));
    jelli_game_resume_begin(&saved.game, 0u);
    CHECK(finish(&saved.game) == 0u && saved.game.sleep_log.count == 1u);
    CHECK(saved.game.ticks == restarted.game.ticks);
}

static void capped_gap_preserves_sleep_and_budget(void)
{
    JelliSave saved = sleeping_checkpoint();
    const uint64_t extra = UINT64_C(3) * 3600000u;
    jelli_game_resume_begin(&saved.game, JELLI_OFFLINE_CAP_MS + extra);
    CHECK(saved.game.discarded_ms == extra);
    CHECK(finish(&saved.game) == 1440u);
    CHECK(saved.game.ticks == JELLI_OFFLINE_CAP_MS / 100u);
    CHECK(saved.game.sleep_log.active && saved.game.sleep_log.count == 1u);
    CHECK(saved.game.pets[0].asleep);
}

int main(void)
{
    overnight_interruption_and_restart();
    capped_gap_preserves_sleep_and_budget();
    puts("PASS: overnight journal survives interrupted catch-up and capped sleep");
    return 0;
}
