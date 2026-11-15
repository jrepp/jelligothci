#include "jelli/invitations.h"
#include "jelli/activities.h"
#include "jelli/save.h"
#include "jelli/collection.h"
#include "save_layout.h"
#include "game_fixture.h"
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
static JelliSave save, loaded, scratch;
static uint8_t bytes[JELLI_SAVE_CAPACITY];

static void init(void)
{
    memset(&save, 0, sizeof(save));
    jelli_game_init(&save.game);
    save.game.wall_known = true;
    save.game.wall_seconds = UINT64_C(10) * 3600u;
    JelliPet *pet = &save.game.pets[0];
    for (unsigned i = 0; i < JELLI_NEED_COUNT; ++i)
        pet->needs[i] = 700u;
    pet->hydration = 700u;
}

static void crc(size_t n)
{
    uint32_t value = UINT32_MAX;
    for (size_t i = 0; i < n - 8u; ++i) {
        value ^= bytes[i];
        for (unsigned b = 0; b < 8u; ++b)
            value = (value >> 1u) ^ ((value & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    for (unsigned i = 0; i < 4u; ++i) {
        bytes[n - 8u + i] = (uint8_t)(~value >> (8u * i));
        bytes[8u + i] = (uint8_t)(n >> (8u * i));
    }
}

static void snuggle_bond_gate(void)
{
    init();
    JelliPet *pet = &save.game.pets[0];
    unsigned id = 0u;
    while (id < jelli_moment_count && strcmp(jelli_moments[id].name, "SNUGGLE") != 0)
        ++id;
    CHECK(id < jelli_moment_count && jelli_moments[id].min_bond == 250u);
    pet->form = 3u;
    pet->bond = 249u;
    CHECK(!jelli_moment_identity_allowed(&save.game, pet, id));
    CHECK(jelli_moment_available(&save.game, pet, id) == JELLI_NOT_READY);
    pet->invitations.pending = UINT32_C(1) << id;
    pet->invitations.announced = pet->invitations.pending;
    pet->invitations.leisure = (uint8_t)(id + 1u);
    pet->invitations.leisure_until = 1000u;
    pet->invitations.clock_basis = 1u;
    CHECK(!jelli_invitation_grants(&save.game, pet, id));
    CHECK(jelli_moment_available(&save.game, pet, id) == JELLI_NOT_READY);
    pet->bond = 250u;
    CHECK(jelli_moment_identity_allowed(&save.game, pet, id));
    CHECK(jelli_moment_available(&save.game, pet, id) == JELLI_OK);
}

static void persisted_and_consumed(void)
{
    init();
    CHECK(jelli_invitations_update(&save.game));
    JelliPet *pet = &save.game.pets[0];
    unsigned id = pet->invitations.leisure - 1u;
    CHECK(id < jelli_moment_count);
    CHECK(jelli_invitation_grants(&save.game, pet, id));
    CHECK(jelli_invitation_seen(pet, id));
    JelliInvitations original = pet->invitations;
    CHECK(!jelli_invitations_update(&save.game));
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode_workspace(&loaded, bytes, size, &scratch));
    CHECK(jelli_invitations_same(&loaded.game.pets[0].invitations, &original));
    loaded.game.wall_known = true;
    loaded.game.wall_seconds = save.game.wall_seconds;
    CHECK(!jelli_invitations_update(&loaded.game));
    JelliCommand cmd = {JELLI_CMD_MOMENT, pet->id, id};
    CHECK(jelli_game_check(&save.game, cmd, &scratch.game) == JELLI_OK);
    CHECK(pet->invitations.leisure == id + 1u); /* Preflight cannot consume. */
    pet->hydration = 0u;
    if (jelli_moments[id].costs[JELLI_ACTIVITY_HYDRATION]) {
        CHECK(jelli_game_command(&save.game, cmd) == JELLI_NOT_READY);
        CHECK(pet->invitations.leisure == id + 1u);
    }
    pet->hydration = 700u;
    CHECK(jelli_game_command(&save.game, cmd) == JELLI_OK);
    CHECK(!pet->invitations.leisure);
    CHECK(!(pet->invitations.pending & (UINT32_C(1) << id)));
    CHECK(pet->invitations.announced & (UINT32_C(1) << id));
}

static void retirement_and_clock(void)
{
    init();
    (void)jelli_invitations_update(&save.game);
    JelliPet *pet = &save.game.pets[0];
    unsigned id = pet->invitations.leisure - 1u;
    uint64_t end = pet->invitations.leisure_until;
    save.game.wall_seconds = (end - 1u) * 60u;
    (void)jelli_invitations_update(&save.game);
    CHECK(jelli_invitation_grants(&save.game, pet, id)); /* Crosses catalog hourly choice. */
    pet->asleep = true;
    pet->nap_due = UINT64_MAX;
    save.game.wall_seconds = end * 60u;
    (void)jelli_invitations_update(&save.game);
    CHECK(!pet->invitations.leisure);
    CHECK(!(pet->invitations.pending & (UINT32_C(1) << id)));
    CHECK(!jelli_invitations_update(&save.game));
    save.game.wall_seconds -= 3600u;
    CHECK(!jelli_invitations_update(&save.game));
    CHECK(!jelli_invitation_grants(&save.game, pet, id));
    save.game.wall_known = false;
    (void)jelli_invitations_update(&save.game);
    CHECK(!pet->invitations.pending && pet->invitations.next_generation > 0u);
}

static void relaxed_access(void)
{
    /* Find an actual generated invitation that the ordinary hourly gate rejects. */
    bool found = false;
    for (unsigned hour = 1u; hour <= 20u && !found; ++hour) {
        init();
        save.game.wall_seconds = (uint64_t)hour * 3600u;
        (void)jelli_invitations_update(&save.game);
        JelliPet *pet = &save.game.pets[0];
        unsigned id = pet->invitations.leisure - 1u;
        CHECK(id < jelli_moment_count);
        JelliInvitations offer = pet->invitations;
        pet->invitations = (JelliInvitations){0};
        if (jelli_moment_available(&save.game, pet, id) == JELLI_NOT_READY) {
            pet->invitations = offer;
            CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_MOMENT, pet->id, id}) ==
                  JELLI_OK);
            found = true;
        }
    }
    CHECK(found);
}

static void timed_and_resume(void)
{
    init();
    save.game.wall_seconds = UINT64_C(10) * 3600u + UINT64_C(59) * 60u;
    CHECK(jelli_invitations_update(&save.game));
    JelliPet *pet = &save.game.pets[0];
    CHECK(pet->invitations.pending & 1u); /* Breakfast is a real timed offer. */
    CHECK(jelli_invitation_seen(pet, 0u));
    JelliInvitations before = pet->invitations;
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_REST, pet->id, 0u}) == JELLI_OK);
    (void)jelli_invitations_update(&save.game);
    CHECK(jelli_invitations_same(&before, &pet->invitations));
    save.game.wall_seconds += 60u;
    (void)jelli_invitations_update(&save.game);
    CHECK(!(pet->invitations.pending & 1u) && !(pet->invitations.seen & 1u));
    CHECK(pet->invitations.announced & 1u);
    jelli_game_resume_begin(&save.game, 7200000u);
    before = pet->invitations;
    CHECK(!jelli_invitations_update(&save.game));
    CHECK(jelli_invitations_same(&before, &pet->invitations));
    while (!jelli_game_resume_step(&save.game)) {
    }
    save.game.wall_seconds += 7200u;
    (void)jelli_invitations_update(&save.game);
    CHECK(!pet->invitations.leisure && !pet->invitations.pending);
    CHECK(!jelli_invitations_update(&save.game));
}

static void per_pet_offers(void)
{
    init();
    test_game_pair(&save.game);
    save.game.wall_known = true;
    save.game.wall_seconds = UINT64_C(10) * 3600u;
    CHECK(jelli_invitations_update(&save.game));
    CHECK(save.game.pets[0].invitations.pending && !save.game.pets[1].invitations.pending);
    JelliInvitations first = save.game.pets[0].invitations;
    CHECK(jelli_game_command(&save.game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    CHECK(jelli_invitations_update(&save.game));
    CHECK(jelli_invitations_same(&first, &save.game.pets[0].invitations));
    JelliPet *second = &save.game.pets[1];
    CHECK(second->invitations.leisure);
    CHECK(jelli_invitation_seen(second, second->invitations.leisure - 1u));
    CHECK(!save.game.pets[0].invitations.seen);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode_workspace(&loaded, bytes, size, &scratch));
    CHECK(jelli_invitations_same(&first, &loaded.game.pets[0].invitations));
    CHECK(jelli_invitations_same(&second->invitations, &loaded.game.pets[1].invitations));
    second->asleep = true;
    second->nap_due = UINT64_MAX;
    uint64_t expiry = first.leisure_until > second->invitations.leisure_until
                          ? first.leisure_until
                          : second->invitations.leisure_until;
    save.game.wall_seconds = expiry * 60u;
    CHECK(jelli_invitations_update(&save.game));
    CHECK(!save.game.pets[0].invitations.pending && !second->invitations.pending);
    CHECK(!jelli_invitations_update(&save.game));
}

static void codec_boundaries(void)
{
    init();
    (void)jelli_invitations_update(&save.game);
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size);
    bytes[size - 9u] = 255u; /* Final pet's invalid clock basis. */
    crc(size);
    CHECK(!jelli_save_decode_workspace(&loaded, bytes, size, &scratch));
    size = jelli_save_encode(&save, bytes, sizeof(bytes));
    size_t tail = TEST_SAVE_V21_BYTES(save.game.count);
    memmove(bytes + size - 8u - tail, bytes + size - 8u, 8u);
    size -= tail;
    bytes[4] = 20u;
    /* Size participates in the checksum. */
    for (unsigned i = 0; i < 4u; ++i)
        bytes[8u + i] = (uint8_t)(size >> (8u * i));
    crc(size);
    CHECK(jelli_save_decode_workspace(&loaded, bytes, size, &scratch));
    CHECK(!loaded.game.pets[0].invitations.pending);
    printf("ledger=%zu pet=%zu game=%zu save=%zu\n", sizeof(JelliInvitations), sizeof(JelliPet),
           sizeof(JelliGame), size + tail);
}

int main(void)
{
    snuggle_bond_gate();
    per_pet_offers();
    persisted_and_consumed();
    retirement_and_clock();
    relaxed_access();
    codec_boundaries();
    timed_and_resume();
    puts("PASS: invitation access, persistence, retirement, rollback and migration");
    return 0;
}
