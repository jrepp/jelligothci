#include "game_fixture.h"
#include "jelli/activities.h"
#include "jelli/pet_rewards.h"
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

static JelliGame game;
static JelliEventLog events;
static JelliPetRewards rewards;

static void reset(void)
{
    test_game_pair(&game);
    events = (JelliEventLog){0};
    rewards = (JelliPetRewards){0};
    game.events = &events;
}

static void command(JelliCommandKind kind, unsigned value)
{
    CHECK(jelli_game_command(&game, (JelliCommand){kind, 1u, value}) == JELLI_OK);
}

static void process(void) { CHECK(!jelli_pet_rewards_process(&rewards, &game, false, false, 0u)); }

static void advance(unsigned milliseconds)
{
    while (milliseconds) {
        unsigned step = milliseconds > 800u ? 800u : milliseconds;
        jelli_game_advance(&game, step);
        milliseconds -= step;
    }
}

static void timed_completion_excludes_unrelated_changes(void)
{
    reset();
    game.clock_known = true;
    game.clock_minute = 780u;
    command(JELLI_CMD_MOMENT, 1u);
    process();
    CHECK(rewards.pending && !rewards.count);
    int energy = rewards.gains[JELLI_ENERGY];
    int social = rewards.gains[JELLI_SOCIAL];
    CHECK(energy == 0 && social == 0);
    command(JELLI_CMD_TOUCH, 0u);
    process();
    CHECK(rewards.gains[JELLI_ENERGY] == energy && rewards.gains[JELLI_SOCIAL] == social);
    advance(8000u);
    process();
    CHECK(!rewards.pending && rewards.count >= 3u && rewards.completed == 1u);
    CHECK(game.pets[0].prize_progress.counts[3] == 1u);
    bool found_energy = false, found_social = false, found_play = false;
    for (unsigned i = 0u; i < rewards.count; ++i) {
        const JelliPetReward *item = &rewards.items[i];
        CHECK(item->to > item->from);
        if (item->stat == 2u) {
            found_energy = true;
            CHECK(item->to - item->from >= 150);
        }
        if (item->stat == 5u) {
            found_social = true;
            CHECK(item->to - item->from >= 180);
        }
        found_play = found_play || item->stat == 4u;
    }
    CHECK(found_energy && found_social && found_play);
    process();
    CHECK(rewards.completed == 1u && game.pets[0].prize_progress.counts[3] == 1u);
}

static void routine_completion_and_abort(void)
{
    reset();
    command(JELLI_CMD_HEALTH, 3u);
    CHECK(!jelli_pet_rewards_process(&rewards, &game, true, false, 1u));
    CHECK(rewards.health && !rewards.count);
    CHECK(!jelli_pet_rewards_process(&rewards, &game, false, false, 1u));
    CHECK(!rewards.pending && !rewards.count && !game.prizes.offered);
    command(JELLI_CMD_HEALTH, 3u);
    CHECK(!jelli_pet_rewards_process(&rewards, &game, true, false, 1u));
    command(JELLI_CMD_HEALTH, 3u);
    CHECK(jelli_pet_rewards_process(&rewards, &game, true, true, 1u));
    CHECK(rewards.count >= 2u && game.prizes.offered == 6u);
    CHECK(!jelli_pet_rewards_process(&rewards, &game, true, true, 1u));
    CHECK(rewards.completed == 1u);
}

static void animation_waits_counts_holds_and_cancels(void)
{
    reset();
    rewards.items[0] = (JelliPetReward){100u, 400u, 3u};
    rewards.items[1] = (JelliPetReward){200u, 500u, 6u};
    rewards.count = 2u;
    JelliParticles particles = {0};
    uint8_t selected = 0u;
    uint16_t value = 0u;
    CHECK(!jelli_pet_rewards_animate(&rewards, &particles, 0u, 900u, false, &selected, &value));
    CHECK(jelli_particles_count(&particles) == 0u);
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 100u, 900u, true, &selected, &value));
    CHECK(selected == 3u && value == 100u && jelli_particles_count(&particles) == 3u);
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 400u, 900u, true, &selected, &value));
    CHECK(value == 250u && jelli_particles_count(&particles) == 3u);
    CHECK(!jelli_pet_rewards_animate(&rewards, &particles, 500u, 900u, false, &selected, &value));
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 1500u, 900u, true, &selected, &value));
    CHECK(value == 250u && jelli_particles_count(&particles) == 3u);
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 1800u, 900u, true, &selected, &value));
    CHECK(value == 400u);
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 2099u, 900u, true, &selected, &value));
    CHECK(selected == 3u && value == 400u);
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 2100u, 900u, true, &selected, &value));
    CHECK(selected == 6u && value == 200u && jelli_particles_count(&particles) == 6u);
    CHECK(jelli_pet_rewards_animate(&rewards, &particles, 2700u, 900u, true, &selected, &value));
    CHECK(value == 500u);
    CHECK(!jelli_pet_rewards_animate(&rewards, &particles, 3000u, 900u, true, &selected, &value));
    CHECK(selected == 6u && !rewards.count);
    rewards.count = 1u;
    rewards.pending = true;
    jelli_pet_rewards_cancel(&rewards);
    CHECK(!rewards.count && rewards.pending && selected == 6u);
}

static void medicine_and_shots_do_not_offer_prizes(void)
{
    for (unsigned activity = 1u; activity <= 2u; ++activity) {
        reset();
        command(JELLI_CMD_HEALTH, activity);
        CHECK(jelli_pet_rewards_process(&rewards, &game, true, true, 1u));
        CHECK(rewards.count && !game.prizes.offered);
        CHECK(game.pets[0].prize_progress.counts[7] == 0u);
    }
}

static void resume_and_history_gap_do_not_replay(void)
{
    reset();
    command(JELLI_CMD_PLAY, 0u);
    process();
    CHECK(rewards.pending);
    game.resuming = true;
    process();
    game.resuming = false;
    advance(8000u);
    process();
    CHECK(!rewards.count && !rewards.completed);
    reset();
    command(JELLI_CMD_PLAY, 0u);
    process();
    for (unsigned i = 0u; i < JELLI_EVENT_CAPACITY + 1u; ++i)
        jelli_events_push(&events, (JelliEvent){.kind = JELLI_EVENT_CHEAT, .pet_id = 1u});
    advance(8000u);
    process();
    CHECK(!rewards.count && !rewards.pending);
}

static void manual_sleep_rewards_only_at_wake(void)
{
    reset();
    command(JELLI_CMD_REST, 0u);
    process();
    JelliPet *pet = &game.pets[0];
    uint16_t bed_energy = pet->needs[JELLI_ENERGY];
    jelli_habits_advance(&pet->habits, 0u, 288000u, true, false);
    pet->ticks = 288000u;
    pet->needs[JELLI_ENERGY] = 1000u;
    process();
    CHECK(!rewards.count);
    command(JELLI_CMD_WAKE, 0u);
    process();
    CHECK(rewards.count == 2u && rewards.items[0].stat == 2u && rewards.items[1].stat == 7u);
    CHECK(rewards.items[0].from == bed_energy && rewards.items[0].to == 1000u);
    CHECK(game.prizes.offered == 7u);
    process();
    CHECK(rewards.completed == 1u);
    jelli_pet_rewards_cancel(&rewards);
    pet->asleep = true;
    pet->nap_due = pet->ticks + 36000u;
    command(JELLI_CMD_WAKE, 0u); /* An automatic nap must not replay the completed diary. */
    process();
    CHECK(!rewards.count && rewards.completed == 1u);
}

static void snuggle_offers_a_giftable_teddy(void)
{
    jelli_game_init(&game);
    test_game_fill(&game, 1u, 3u, 1u); /* Pet 3 is BUBBLE, a baby axolotl. */
    events = (JelliEventLog){0};
    rewards = (JelliPetRewards){0};
    game.events = &events;
    game.active = 2u;
    game.clock_known = true;
    game.clock_minute = 780u;
    CHECK(game.pets[2].form == 3u);
    unsigned snuggle = 0u;
    while (snuggle < jelli_moment_count && strcmp(jelli_moments[snuggle].name, "SNUGGLE") != 0)
        ++snuggle;
    CHECK(snuggle < jelli_moment_count);
    unsigned teddy = jelli_moments[snuggle].prize;
    CHECK(teddy > 0u && teddy <= jelli_prize_count);
    CHECK(strcmp(jelli_prizes[teddy - 1u].key, "teddy-bear") == 0);
    CHECK(jelli_moment_available(&game, &game.pets[0], snuggle) == JELLI_NOT_READY);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, 3u, snuggle}) == JELLI_OK);
    process();
    advance(8000u);
    process();
    CHECK(rewards.completed == 1u && game.prizes.offered == jelli_moments[snuggle].prize);
    CHECK(game.prizes.offered_pet == 3u && jelli_prize_catch(&game) == JELLI_OK);
    JelliSave save = {.game = game}, loaded;
    save.game.events = NULL;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.prizes.origin_pet[jelli_moments[snuggle].prize - 1u] == 3u);
    /* Like every present, the teddy goes to another pet, not back to the one that earned it. */
    CHECK(jelli_prize_gift(&game, jelli_moments[snuggle].prize - 1u) == JELLI_NOT_READY);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 3u, 1u}) == JELLI_OK);
    CHECK(jelli_prize_gift(&game, jelli_moments[snuggle].prize - 1u) == JELLI_OK);
    CHECK(!(game.prizes.owned & (1u << (jelli_moments[snuggle].prize - 1u))));
    CHECK(game.prizes.offered == 9u); /* Gifting earns the friendship bow. */
}

static void save_and_reload(void)
{
    JelliSave save = {.game = game}, loaded;
    save.game.events = NULL;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    game = loaded.game;
    events = (JelliEventLog){0};
    rewards = (JelliPetRewards){0};
    game.events = &events;
}

/* A free replay accepts without paying, then completes without rewards or a teddy. */
static void snuggle_replays_free(unsigned snuggle)
{
    JelliPet *bubble = &game.pets[2];
    uint16_t energy = bubble->needs[JELLI_ENERGY], bond = bubble->bond;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, 3u, snuggle}) == JELLI_OK);
    CHECK(bubble->needs[JELLI_ENERGY] == energy &&
          (bubble->behavior_flags & JELLI_PET_FLAG_FREE_PLAY));
    process();
    save_and_reload(); /* Mid-run: the saved flag keeps the replay unpaid. */
    bubble = &game.pets[2];
    uint16_t social = bubble->needs[JELLI_SOCIAL];
    advance(8000u);
    process();
    CHECK(!bubble->moment && bubble->needs[JELLI_SOCIAL] <= social && bubble->bond == bond);
    CHECK(game.prizes.offered != jelli_moments[snuggle].prize && !rewards.count);
}

static void snuggle_rewards_once_per_day(void)
{
    snuggle_offers_a_giftable_teddy(); /* Rewarded snuggle; teddy caught and gifted to pet 1. */
    CHECK(jelli_prize_catch(&game) == JELLI_OK); /* Clear the bow so a new offer could show. */
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 3u}) == JELLI_OK);
    unsigned snuggle = 0u;
    while (strcmp(jelli_moments[snuggle].name, "SNUGGLE") != 0)
        ++snuggle;
    CHECK(jelli_moments[snuggle].daily_reward);
    snuggle_replays_free(snuggle); /* Gifting the teddy away does not re-arm it. */
    save_and_reload();             /* Neither does a save/reload between runs. */
    snuggle_replays_free(snuggle);
    /* A free replay needs no energy; a rewarded run would. */
    game.pets[2].needs[JELLI_ENERGY] = 0u;
    CHECK(jelli_moment_available(&game, &game.pets[2], snuggle) == JELLI_OK);
    /* The next local day pays costs and offers the teddy again. */
    game.pets[2].needs[JELLI_ENERGY] = 1000u;
    game.pets[2].activity_day = (uint16_t)(game.pets[2].activity_day + 1u);
    uint16_t bond = game.pets[2].bond;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, 3u, snuggle}) == JELLI_OK);
    CHECK(game.pets[2].needs[JELLI_ENERGY] < 1000u &&
          !(game.pets[2].behavior_flags & JELLI_PET_FLAG_FREE_PLAY));
    process();
    advance(8000u);
    process();
    CHECK(game.pets[2].bond > bond && game.prizes.offered == jelli_moments[snuggle].prize);
}

int main(void)
{
    snuggle_offers_a_giftable_teddy();
    snuggle_rewards_once_per_day();
    timed_completion_excludes_unrelated_changes();
    routine_completion_and_abort();
    medicine_and_shots_do_not_offer_prizes();
    animation_waits_counts_holds_and_cancels();
    resume_and_history_gap_do_not_replay();
    manual_sleep_rewards_only_at_wake();
    puts("PASS: completion rewards, isolated gains, routine abort, counting, sleep and replay "
         "guards");
    return 0;
}
