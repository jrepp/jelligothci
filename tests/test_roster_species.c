/* Seeded property walk over jelli_game_add_species on random rosters: two to eight
 * pets with IDs drawn from 1..12 (so 3 and 4 are often taken), random forms, active
 * slots and prize sources, plus rosters broken in one known way (too short, oversized,
 * zero or duplicate ID, unknown form, active past the end, an offered present from an
 * unknown pet). For every form 0..5 and 255 the result matches an oracle; a refusal
 * leaves the game byte-identical; an append keeps every earlier pet and the active
 * slot and adds one fresh pet of that form at the first unused ID from the species'
 * stable ID; a second call is refused; and adding the ninja and the unicorn in either
 * order ends with the same forms when both fit. A failure prints the seed. */
#include "jelli/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_SEED(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x)\n", __FILE__, __LINE__, #expression,          \
                    (unsigned)current_seed);                                                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { CASES = 20000, ID_POOL = 12 };
static uint32_t current_seed, state;

static uint32_t next_random(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* A roster valid as stored: distinct IDs, valid pets, consistent prize sources. */
static JelliGame random_roster(unsigned count)
{
    JelliGame game;
    jelli_game_init(&game);
    const JelliPet template = game.pets[0];
    uint32_t pool[ID_POOL];
    for (unsigned i = 0u; i < ID_POOL; ++i)
        pool[i] = i + 1u;
    for (unsigned i = ID_POOL - 1u; i > 0u; --i) {
        unsigned j = next_random() % (i + 1u);
        uint32_t swap = pool[i];
        pool[i] = pool[j];
        pool[j] = swap;
    }
    for (unsigned p = 0u; p < JELLI_PET_CAPACITY; ++p) {
        game.pets[p] = p < count ? template : (JelliPet){0};
        if (p >= count)
            continue;
        game.pets[p].id = game.pets[p].random_state = pool[p];
        game.pets[p].form = (uint8_t)(next_random() % JELLI_FORM_COUNT);
        game.pets[p].bond = (uint16_t)(next_random() % 1001u);
    }
    game.count = (uint8_t)count;
    game.active = (uint8_t)(next_random() % count);
    if (next_random() % 3u == 0u) {
        game.prizes.offered = 1u;
        game.prizes.offered_pet = game.pets[next_random() % count].id;
    }
    return game;
}

/* Breaks a valid roster in one way the stored-roster check must catch. */
static void break_roster(JelliGame *game, unsigned how)
{
    if (how == 0u) { /* One pet and otherwise valid: only the two-pet minimum refuses it. */
        for (unsigned p = 1u; p < JELLI_PET_CAPACITY; ++p)
            game->pets[p] = (JelliPet){0};
        game->count = 1u;
        game->active = 0u;
        game->prizes.offered_pet = game->prizes.offered ? game->pets[0].id : 0u;
    } else if (how == 1u)
        game->count = (uint8_t)(JELLI_PET_CAPACITY + 1u);
    else if (how == 2u)
        game->pets[next_random() % game->count].id = 0u;
    else if (how == 3u && game->count > 1u)
        game->pets[1].id = game->pets[0].id;
    else if (how == 4u)
        game->pets[next_random() % game->count].form =
            (uint8_t)(JELLI_FORM_COUNT + next_random() % 4u);
    else if (how == 5u)
        game->active = game->count;
    else {
        game->prizes.offered = 1u;
        game->prizes.offered_pet = 99u; /* No such pet. */
    }
}

static bool has_form(const JelliGame *game, uint8_t form)
{
    for (unsigned p = 0u; p < game->count && p < JELLI_PET_CAPACITY; ++p)
        if (game->pets[p].form == form)
            return true;
    return false;
}

static bool id_used(const JelliGame *game, uint32_t id)
{
    for (unsigned p = 0u; p < game->count; ++p)
        if (game->pets[p].id == id)
            return true;
    return false;
}

/* Raw bytes, padding included: a refusal must not write anything. */
static bool unchanged(const uint8_t *before, const JelliGame *game)
{
    static uint8_t after[sizeof(JelliGame)];
    memcpy(after, game, sizeof(after));
    return memcmp(before, after, sizeof(after)) == 0;
}

static void check_add(JelliGame *game, uint8_t form, bool valid)
{
    static uint8_t before[sizeof(JelliGame)];
    JelliGame original = *game, reference;
    memcpy(before, game, sizeof(before));
    uint32_t id = form == JELLI_FORM_NINJA     ? JELLI_NINJA_PET_ID
                  : form == JELLI_FORM_UNICORN ? JELLI_UNICORN_PET_ID
                                               : 0u;
    bool expected = id && valid && game->count < JELLI_PET_CAPACITY && !has_form(game, form);
    CHECK_SEED(jelli_game_add_species(game, form) == expected);
    if (!expected) {
        CHECK_SEED(unchanged(before, game));
        return;
    }
    while (id_used(&original, id))
        ++id;
    jelli_game_init(&reference);
    JelliPet fresh = reference.pets[form == JELLI_FORM_NINJA ? 2u : 3u];
    fresh.id = fresh.random_state = id;
    const JelliPet *added = &game->pets[original.count];
    CHECK_SEED(game->count == original.count + 1u && game->active == original.active);
    CHECK_SEED(memcmp(game->pets, original.pets, sizeof(JelliPet) * original.count) == 0);
    CHECK_SEED(added->id == fresh.id && added->form == form && added->bond == fresh.bond &&
               added->random_state == fresh.random_state && added->ticks == 0u);
    CHECK_SEED(memcmp(added->needs, fresh.needs, sizeof(fresh.needs)) == 0);
    CHECK_SEED(game->count < JELLI_ROSTER_MIN || jelli_game_valid(game));
    memcpy(before, game, sizeof(before));
    CHECK_SEED(!jelli_game_add_species(game, form)); /* Idempotent: never a second one. */
    CHECK_SEED(unchanged(before, game));
}

/* Forms present after adding both species, in the given order. */
static unsigned forms_after(JelliGame game, bool ninja_first)
{
    (void)jelli_game_add_species(&game, ninja_first ? JELLI_FORM_NINJA : JELLI_FORM_UNICORN);
    (void)jelli_game_add_species(&game, ninja_first ? JELLI_FORM_UNICORN : JELLI_FORM_NINJA);
    unsigned forms = 0u;
    for (unsigned p = 0u; p < game.count; ++p)
        forms |= 1u << game.pets[p].form;
    return forms | (unsigned)game.count << 8u;
}

int main(void)
{
    static const uint8_t forms[] = {0u, 1u, 2u, 3u, 4u, 5u, 255u};
    unsigned appended = 0u, refused = 0u, broken = 0u;
    CHECK_SEED(!jelli_game_add_species(NULL, JELLI_FORM_NINJA));
    for (unsigned i = 0u; i < CASES; ++i) {
        current_seed = state = 0x5be1e500u + i * 0x9e3779b9u | 1u;
        unsigned count = 2u + next_random() % (JELLI_PET_CAPACITY - 1u);
        JelliGame game = random_roster(count);
        bool valid = next_random() % 6u != 0u;
        if (!valid) {
            break_roster(&game, next_random() % 7u);
            ++broken;
        }
        if (game.count + 2u <= JELLI_PET_CAPACITY) /* With one free slot, the first wins. */
            CHECK_SEED(forms_after(game, true) == forms_after(game, false));
        for (size_t f = 0u; f < sizeof(forms); ++f) {
            unsigned before = game.count;
            check_add(&game, forms[f], valid);
            appended += game.count != before ? 1u : 0u;
            refused += game.count == before ? 1u : 0u;
        }
    }
    printf("species: %u rosters (%u broken), %u appends, %u refusals\n", (unsigned)CASES, broken,
           appended, refused);
    current_seed = 0u;
    CHECK_SEED(appended > CASES / 3u && refused > CASES * 4u && broken > CASES / 10u);
    puts("PASS: species append properties on random rosters");
    return 0;
}
