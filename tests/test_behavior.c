#include "jelli/activities.h"
#include "jelli/behavior.h"
#include "jelli/collection.h"
#include "jelli/potty.h"
#include "jelli/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned state_named(const char *name)
{
    for (unsigned i = 0u; i < jelli_behavior_state_count; ++i)
        if (strcmp(jelli_behavior_states[i].name, name) == 0)
            return i + 1u; /* As stored in pet->behavior. */
    fprintf(stderr, "no state %s\n", name);
    exit(1);
}

static JelliPet *active(JelliGame *game) { return &game->pets[game->active]; }

/* One simulated second; a single advance call admits at most eight 100 ms ticks. */
static void second(JelliGame *game)
{
    jelli_game_advance(game, 500u);
    jelli_game_advance(game, 500u);
}

/* BUBBLE (entry 3, the axolotl) active, starting a known number of seconds into its life. */
static void bubble_game(JelliGame *game, unsigned skip)
{
    jelli_game_init(game);
    game->prizes.offered = 6u;
    game->prizes.offered_pet = game->pets[0].id;
    CHECK(jelli_prize_catch(game) == JELLI_OK);
    int index = jelli_collection_find(game, 3u);
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_ACTIVATE, game->pets[0].id,
                                                  game->pets[index].id}) == JELLI_OK);
    active(game)->ticks += (uint64_t)skip * JELLI_BEHAVIOR_TICKS;
    second(game); /* Settle: drains stimuli from setup. */
    if (active(game)->behavior) {
        active(game)->behavior = 0u;
        active(game)->behavior_left = 0u;
    }
}

/* No jelly reaction (MINT form 0, LILAC form 1) answers the potty urge or leads to a potty
 * request, study or contemplation, so jellies never have accidents. */
static void jelly_tables_are_gentle(void)
{
    const unsigned forbidden[] = {state_named("asking_potty"), state_named("studying"),
                                  state_named("contemplating")};
    for (unsigned form = 0u; form < 2u; ++form) {
        unsigned index = jelli_behavior_form_repertoire[form];
        CHECK(index < jelli_behavior_repertoire_count);
        const JelliRepertoire *rep = &jelli_behavior_repertoires[index];
        CHECK(rep->reaction_count > 0u);
        for (unsigned i = 0u; i < rep->reaction_count; ++i) {
            const JelliBehaviorReaction *r = &jelli_behavior_reactions[rep->first_reaction + i];
            CHECK(r->on != JELLI_STIM_POTTY_URGE);
            for (unsigned f = 0u; f < sizeof(forbidden) / sizeof(forbidden[0]); ++f)
                CHECK(r->state + 1u != forbidden[f]); /* Tables count states from 0. */
        }
    }
}

/* Mint delights at presents and never gets a mess, even with a full potty urge. */
static void mint_jelly_is_gentle(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.pets[0].needs[JELLI_SATIETY] = 100u;
    game.pets[0].potty = 999u;
    unsigned delighted = 0u;
    for (unsigned s = 0u; s < 600u; ++s) {
        jelli_behavior_stimulus(&game, JELLI_STIM_PRESENT_CAUGHT, 0u);
        second(&game);
        unsigned state = game.pets[0].behavior;
        CHECK(state != state_named("asking_potty") && state != state_named("studying"));
        delighted += state == state_named("delighted");
        CHECK(!(game.pets[0].behavior_flags & JELLI_PET_FLAG_MESS));
    }
    CHECK(delighted > 0u);
}

static void presents_make_bubble_curious(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    game.prizes.offered = 1u;
    game.prizes.offered_pet = active(&game)->id;
    CHECK(jelli_prize_catch(&game) == JELLI_OK);
    second(&game);
    unsigned state = active(&game)->behavior;
    CHECK(state == state_named("curious") || state == state_named("delighted"));
    CHECK(active(&game)->behavior_left > 0u && jelli_game_valid(&game));
}

static void requests_are_answered(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->digesting = 1000u;
    pet->potty = (uint16_t)(jelli_potty_rules.urge_threshold - 1u);
    for (unsigned s = 0u; s < 120u && pet->behavior != state_named("asking_potty"); ++s)
        second(&game);
    CHECK(pet->behavior == state_named("asking_potty"));
    uint16_t bond = pet->bond;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, pet->id, 0u}) == JELLI_OK);
    CHECK(pet->behavior == state_named("asking_potty")); /* Feeding is not the request. */
    while (pet->activity != JELLI_IDLE)
        second(&game);
    CHECK(jelli_game_command(
              &game, (JelliCommand){JELLI_CMD_HEALTH, pet->id, JELLI_HEALTH_POTTY}) == JELLI_OK);
    CHECK(pet->behavior == 0u && pet->cooldown_state == state_named("asking_potty"));
    CHECK(pet->bond >= bond + jelli_behavior_states[state_named("asking_potty") - 1u].request_bond);
}

static void hunger_asks_for_food_then_times_out(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->needs[JELLI_SATIETY] = (uint16_t)(jelli_behavior_rules.need_low - 1u);
    second(&game);
    CHECK(pet->behavior == state_named("asking_food"));
    unsigned left = pet->behavior_left;
    for (unsigned s = 0u; s < left; ++s)
        second(&game);
    CHECK(pet->behavior != state_named("asking_food")); /* No penalty, just a cooldown. */
    CHECK(pet->cooldown_state == state_named("asking_food") && pet->cooldown_left > 0u);
    pet->needs[JELLI_SATIETY] = 900u;
    second(&game);
    pet->needs[JELLI_SATIETY] = (uint16_t)(jelli_behavior_rules.need_low - 1u);
    second(&game);
    CHECK(pet->behavior != state_named("asking_food")); /* Still cooling down. */
}

static void reading_leads_to_study(void)
{
    unsigned studied = 0u;
    for (unsigned trial = 0u; trial < 40u; ++trial) {
        JelliGame game;
        bubble_game(&game, trial * 7u);
        const JelliPet *pet = active(&game);
        CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, pet->id, 4u}) == JELLI_OK);
        for (unsigned s = 0u; s < 20u && pet->activity != JELLI_IDLE; ++s)
            second(&game);
        second(&game);
        studied += pet->behavior == state_named("studying") ||
                   pet->behavior == state_named("contemplating");
    }
    CHECK(studied >= 20u && studied < 40u); /* chance_pct 80 across varied ticks. */
}

static void touch_ends_study(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->behavior = (uint8_t)state_named("studying");
    pet->behavior_left = 30u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_TOUCH, pet->id, 0u}) == JELLI_OK);
    second(&game);
    CHECK(pet->behavior != state_named("studying"));
}

static void offline_only_expires(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->behavior = (uint8_t)state_named("studying");
    pet->behavior_left = 5u;
    pet->needs[JELLI_SATIETY] = 100u;
    jelli_game_resume_begin(&game, 60000u);
    while (!jelli_game_resume_step(&game)) {
    }
    CHECK(pet->behavior == 0u); /* Expired offline, and hunger did not start a request. */
    pet->behavior = (uint8_t)state_named("asking_potty");
    pet->behavior_left = 2u;
    pet->potty = 800u;
    jelli_game_resume_begin(&game, 5000u);
    while (!jelli_game_resume_step(&game)) {
    }
    CHECK(pet->behavior == 0u);
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
    CHECK(pet->potty == 800u); /* Unseen requests expire without a timeout accident. */
}

static void saves_and_affinity(void)
{
    JelliGame game, mint;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->behavior = (uint8_t)state_named("contemplating");
    pet->behavior_left = 12u;
    pet->cooldown_state = (uint8_t)state_named("curious");
    pet->cooldown_left = 9u;
    JelliSave save = {.game = game}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    const JelliPet *back = &loaded.game.pets[loaded.game.active];
    CHECK(back->behavior == pet->behavior && back->behavior_left == 12u);
    CHECK(back->cooldown_state == pet->cooldown_state && back->cooldown_left == 9u);
    pet->behavior = 0u;
    pet->behavior_left = 0u;
    jelli_game_init(&mint);
    pet->needs[JELLI_AMUSEMENT] = mint.pets[0].needs[JELLI_AMUSEMENT] = 100u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, pet->id, 4u}) == JELLI_OK);
    CHECK(jelli_game_command(&mint, (JelliCommand){JELLI_CMD_MOMENT, 1u, 4u}) == JELLI_OK);
    for (unsigned i = 0u; i < 10u; ++i) {
        jelli_game_advance(&game, 800u);
        jelli_game_advance(&mint, 800u);
    }
    unsigned bubble_gain = pet->needs[JELLI_AMUSEMENT] - 100u;
    unsigned mint_gain = mint.pets[0].needs[JELLI_AMUSEMENT] - 100u;
    CHECK(jelli_behavior_moment_percent(pet, 4u) == 150u);
    CHECK(bubble_gain > mint_gain); /* BUBBLE enjoys reading. */
}

static void deterministic(void)
{
    JelliGame a, b;
    bubble_game(&a, 3u);
    bubble_game(&b, 3u);
    for (unsigned s = 0u; s < 900u; ++s) {
        second(&a);
        second(&b);
        CHECK(active(&a)->behavior == active(&b)->behavior);
        CHECK(memcmp(active(&a)->needs, active(&b)->needs, sizeof(active(&a)->needs)) == 0);
    }
}

static void ignored_potty_request_leaves_a_mess(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->behavior = (uint8_t)state_named("asking_potty");
    pet->behavior_left = 2u;
    pet->potty = 800u;
    pet->needs[JELLI_HYGIENE] = 600u;
    second(&game);
    second(&game);
    CHECK(pet->behavior != state_named("asking_potty"));
    CHECK((pet->behavior_flags & JELLI_PET_FLAG_MESS) && pet->potty == 0u);
    CHECK(pet->needs[JELLI_HYGIENE] <= 600u - jelli_potty_rules.accident_hygiene);
    unsigned before = pet->needs[JELLI_HYGIENE];
    for (unsigned s = 0u; s < 120u; ++s)
        second(&game); /* The mess keeps costing hygiene each minute. */
    CHECK(pet->needs[JELLI_HYGIENE] < before);
    JelliSave save = {.game = game}, loaded;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size));
    CHECK(loaded.game.pets[loaded.game.active].behavior_flags & JELLI_PET_FLAG_MESS);
    while (pet->activity != JELLI_IDLE || pet->behavior)
        second(&game);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_CLEAN, pet->id, 0u}) == JELLI_OK);
    for (unsigned s = 0u; s < 20u; ++s) { /* Advance until the clean-up effect lands. */
        second(&game);
        if (active(&game)->activity == JELLI_IDLE)
            break;
    }
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
}

static void answered_potty_request_leaves_no_mess(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->behavior = (uint8_t)state_named("asking_potty");
    pet->behavior_left = 30u;
    pet->potty = 800u;
    CHECK(jelli_game_command(
              &game, (JelliCommand){JELLI_CMD_HEALTH, pet->id, JELLI_HEALTH_POTTY}) == JELLI_OK);
    for (unsigned s = 0u; s < 60u; ++s)
        second(&game);
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
}

static void busy_potty_request_is_deferred(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->ticks = 590u;
    pet->potty = (uint16_t)(jelli_potty_rules.urge_threshold - 1u);
    pet->digesting = 1000u;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_MOMENT, pet->id, 4u}) == JELLI_OK);
    second(&game);
    CHECK(pet->potty >= jelli_potty_rules.urge_threshold);
    CHECK(pet->activity != JELLI_IDLE && !pet->behavior);
    for (unsigned s = 0u; s < 120u && pet->behavior != state_named("asking_potty"); ++s)
        second(&game);
    CHECK(pet->behavior == state_named("asking_potty"));
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
}

static void offline_potty_request_waits_for_live_play(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->potty = (uint16_t)(jelli_potty_rules.urge_threshold - 1u);
    pet->digesting = 1000u;
    jelli_game_resume_begin(&game, 60000u);
    while (!jelli_game_resume_step(&game)) {
    }
    CHECK(pet->potty >= jelli_potty_rules.urge_threshold && !pet->behavior);
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
    second(&game);
    CHECK(pet->behavior == state_named("asking_potty"));
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
}

static void potty_request_respects_sleep_cooldown_and_relief(void)
{
    JelliGame game;
    bubble_game(&game, 0u);
    JelliPet *pet = active(&game);
    pet->potty = jelli_potty_rules.urge_threshold;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, pet->id, 0u}) == JELLI_OK);
    pet->cooldown_state = (uint8_t)state_named("asking_potty");
    pet->cooldown_left = 3u;
    second(&game);
    CHECK(pet->asleep && !pet->behavior);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_WAKE, pet->id, 0u}) == JELLI_OK);
    second(&game);
    CHECK(pet->behavior != state_named("asking_potty"));
    for (unsigned s = 0u; s < 120u && pet->behavior != state_named("asking_potty"); ++s)
        second(&game);
    CHECK(pet->behavior == state_named("asking_potty"));
    CHECK(jelli_game_command(
              &game, (JelliCommand){JELLI_CMD_HEALTH, pet->id, JELLI_HEALTH_POTTY}) == JELLI_OK);
    for (unsigned s = 0u; s < 70u; ++s) {
        second(&game);
        CHECK(pet->behavior != state_named("asking_potty"));
    }
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
}

int main(void)
{
    busy_potty_request_is_deferred();
    offline_potty_request_waits_for_live_play();
    potty_request_respects_sleep_cooldown_and_relief();
    ignored_potty_request_leaves_a_mess();
    answered_potty_request_leaves_no_mess();
    jelly_tables_are_gentle();
    mint_jelly_is_gentle();
    presents_make_bubble_curious();
    requests_are_answered();
    hunger_asks_for_food_then_times_out();
    reading_leads_to_study();
    touch_ends_study();
    offline_only_expires();
    saves_and_affinity();
    deterministic();
    puts("PASS: stimuli drive BUBBLE's data-defined states, requests, cooldowns and affinity");
    return 0;
}
