/* The three-to-eight pet roster: frame selection for every form and state, the ninja
 * over days of simulated time, and SWITCH PET with its preview for every roster size. */
#include "jelli/assets.h"
#include "jelli/debug.h"
#include "../core/pet_draw.h" /* Internal frame selection under test. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STRIDE = JELLI_WIDTH + 5, PIXELS = STRIDE * JELLI_HEIGHT, POSES = 8 };
static uint16_t pixels[PIXELS];

/* Asset IDs in pose order, transcribed from assets/slice/assets.json. */
static const uint32_t pose_ids[3][POSES] = {
    {1001u, 1002u, 1003u, 1004u, 1005u, 1006u, 1021u, 1022u},
    {1007u, 1008u, 1009u, 1010u, 1011u, 1012u, 1023u, 1024u},
    {1031u, 1032u, 1033u, 1034u, 1035u, 1036u, 1037u, 1038u}};

static int pose_of(unsigned form, uint32_t id)
{
    for (int pose = 0; pose < POSES; ++pose)
        if (pose_ids[form][pose] == id)
            return pose;
    return -1;
}

static uint32_t frame_for(uint8_t form, const JelliPetRenderKey *state)
{
    JelliPetUi ui;
    jelli_pet_ui_init(&ui);
    JelliPetRenderKey view = *state;
    view.form = form;
    jelli_pet_actor_layout(&ui, &view);
    CHECK(ui.actor_frame != NULL && jelli_asset_find(ui.actor_frame->id) == ui.actor_frame);
    return ui.actor_frame->id;
}

/* Metamorphic: for any state, every form shows the same pose, and an unknown form
 * shows exactly the Mint frame. 256 forms x 2 x 3 x 6 x 4 x 6 states. */
static void every_form_and_state(void)
{
    static const uint8_t phases[] = {0u, 1u, 2u, 3u, 4u, 255u};
    unsigned states = 0u;
    for (unsigned bits = 0u; bits < 2u * 3u * 6u * 4u; ++bits) {
        for (unsigned p = 0u; p < sizeof(phases); ++p) {
            JelliPetRenderKey state = {.asleep = (bits & 1u) != 0u,
                                       .health = (uint8_t)(bits / 2u % 3u),
                                       .activity = (uint8_t)(bits / 6u % 6u),
                                       .reaction = (uint8_t)(bits / 36u % 4u),
                                       .phase = phases[p]};
            int pose = pose_of(0u, frame_for(0u, &state));
            CHECK(pose >= 0);
            CHECK(pose_of(1u, frame_for(1u, &state)) == pose);
            CHECK(pose_of(2u, frame_for(2u, &state)) == pose);
            for (unsigned form = JELLI_FORM_COUNT; form < 256u; ++form)
                CHECK(frame_for((uint8_t)form, &state) == pose_ids[0][pose]);
            ++states;
        }
    }
    CHECK(states == 864u);
}

/* Every ninja frame exists as a grounded 32x32 creature with consistent bounds. */
static void ninja_assets_resolve(void)
{
    for (unsigned pose = 0u; pose < POSES; ++pose) {
        const JelliAsset *a = jelli_asset_find(pose_ids[2][pose]);
        CHECK(a && a->width == 32u && a->height == 32u && a->mask_stride == 4u);
        CHECK(a->left < a->right && a->top < a->bottom && a->bottom <= 32u);
        CHECK(a->ground_y_q8 == a->bottom * 256u && a->ground_x_q8 > a->left * 256u &&
              a->ground_x_q8 < a->right * 256u);
    }
}

static void resume_ms(JelliGame *game, uint64_t milliseconds)
{
    jelli_game_resume_begin(game, milliseconds);
    while (!jelli_game_resume_step(game))
        CHECK(jelli_game_valid(game));
}

/* Two offline days as the ninja never change its form; a Mint evolves once, to Lilac,
 * and stays Lilac. Only the active pet's clock runs. */
static void forms_over_days(void)
{
    JelliGame game;
    jelli_game_init(&game);
    game.active = 2u;
    for (unsigned day = 0u; day < 2u; ++day) {
        resume_ms(&game, JELLI_OFFLINE_CAP_MS);
        CHECK(game.pets[2].form == JELLI_FORM_NINJA && jelli_game_valid(&game));
    }
    CHECK(game.pets[2].ticks == UINT64_C(2) * JELLI_DAY_TICKS && game.pets[2].stage_ticks > 0u);
    CHECK(game.pets[0].form == JELLI_FORM_MINT && game.pets[1].form == JELLI_FORM_MINT);
    CHECK(game.pets[0].ticks == 0u && game.pets[1].ticks == 0u);
    jelli_game_init(&game);
    resume_ms(&game, 60000u);
    CHECK(game.pets[0].form == JELLI_FORM_LILAC);
    resume_ms(&game, JELLI_OFFLINE_CAP_MS);
    CHECK(game.pets[0].form == JELLI_FORM_LILAC && game.pets[2].form == JELLI_FORM_NINJA);
}

static JelliSurface surface_for(uint16_t *buffer)
{
    return (JelliSurface){buffer, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
}

/* Count pets with distinct IDs and varied forms; the ninja sits at ninja_at. */
static void build_roster(JelliGame *game, unsigned count, unsigned ninja_at)
{
    jelli_game_init(game);
    JelliPet mint = game->pets[0];
    game->count = (uint8_t)count;
    for (unsigned p = 0u; p < count; ++p) {
        game->pets[p] = mint;
        game->pets[p].id = 10u + p;
        game->pets[p].random_state = 10u + p;
        game->pets[p].form = p == ninja_at ? (uint8_t)JELLI_FORM_NINJA : (uint8_t)(p % 2u);
        game->pets[p].stage_ticks = 0u;
        /* Odd bystanders rest, so the Settings preview's asleep flag varies. */
        game->pets[p].asleep = game->pets[p].scheduled_sleep = (p % 2u) == 1u;
    }
    CHECK(jelli_game_valid(game));
}

static void tap_switch(JelliPetUi *ui, JelliGame *game)
{
    ui->menu_open = true;
    ui->page = JELLI_UI_SETTINGS;
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(ui, 2u, game->pets[game->active].asleep, &button));
    CHECK(strcmp(button.label, "SWITCH PET") == 0);
    jelli_pet_ui_tap(ui, game, 356, 111);
}

static void check_stored(JelliPetUi *ui, const JelliGame *game)
{
    JelliSurface s = surface_for(pixels);
    ui->menu_open = true;
    ui->page = JELLI_UI_SETTINGS;
    jelli_pet_render(&s, game, ui, 0u, false);
    const JelliPet *next = &game->pets[(game->active + 1u) % game->count];
    CHECK(jelli_game_next_slot(game) == (game->active + 1u) % game->count);
    CHECK(ui->last_view.active_id == game->pets[game->active].id);
    CHECK(ui->last_view.stored_id == next->id && ui->last_view.stored_form == next->form);
    CHECK(ui->last_view.stored_asleep == next->asleep && ui->last_view.count == game->count);
}

/* Busy, sleep-logged, and resuming pets keep their slot; nothing else changes. */
static void switch_refused(JelliGame *game)
{
    JelliPetUi ui;
    jelli_pet_ui_init(&ui);
    uint8_t start = game->active;
    JelliPet *pet = &game->pets[start];
    pet->asleep = pet->scheduled_sleep = false;
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_FEED, pet->id, 0u}) == JELLI_OK);
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    CHECK(jelli_pet_ui_available(&ui, game, 2u) == JELLI_NOT_READY);
    tap_switch(&ui, game);
    CHECK(ui.result == JELLI_NOT_READY && game->active == start);
    pet->activity = JELLI_IDLE;
    pet->interaction_due = 0u;
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_REST, pet->id, 0u}) == JELLI_OK);
    CHECK(game->sleep_log.active);
    tap_switch(&ui, game);
    CHECK(ui.result == JELLI_NOT_READY && game->active == start);
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_WAKE, pet->id, 0u}) == JELLI_OK);
    jelli_game_resume_begin(game, 1000u);
    tap_switch(&ui, game);
    CHECK(ui.result != JELLI_OK && game->active == start);
    resume_ms(game, 0u);
    CHECK(jelli_game_valid(game));
}

/* SWITCH PET visits every slot in roster order and wraps, for three to eight pets and
 * the ninja in every slot; the Settings preview always describes the next pet. */
static void switch_every_roster(void)
{
    JelliGame odd;
    jelli_game_init(&odd);
    odd.active = odd.count; /* Never index past the roster, even for a corrupt game. */
    CHECK(jelli_game_next_slot(&odd) == 0u);
    odd.count = 0u;
    odd.active = 0u;
    CHECK(jelli_game_next_slot(&odd) == 0u && jelli_game_next_slot(NULL) == 0u);
    unsigned taps = 0u;
    for (unsigned count = JELLI_ROSTER_MIN; count <= JELLI_PET_CAPACITY; ++count) {
        for (unsigned ninja_at = 0u; ninja_at < count; ++ninja_at) {
            JelliGame game;
            JelliPetUi ui;
            build_roster(&game, count, ninja_at);
            jelli_pet_ui_init(&ui);
            for (unsigned step = 0u; step <= count; ++step, ++taps) {
                check_stored(&ui, &game);
                uint8_t expected = jelli_game_next_slot(&game);
                tap_switch(&ui, &game);
                CHECK(ui.result == JELLI_OK && game.active == expected && jelli_game_valid(&game));
            }
            CHECK(game.active == 1u);
            switch_refused(&game);
        }
    }
    CHECK(taps == 3u * 4u + 4u * 5u + 5u * 6u + 6u * 7u + 7u * 8u + 8u * 9u);
}

static uint64_t engine_now(void *ctx)
{
    (void)ctx;
    return 0u;
}

static void engine_present(void *ctx, const JelliSurface *surface)
{
    (void)ctx;
    (void)surface;
}

/* The debug state names the next pet for the busiest roster. */
static void debug_state_names_next_pet(void)
{
    static JelliPetEngine engine;
    static JelliDebug debug;
    JelliPlatform platform = {.now_ms = engine_now, .present = engine_present};
    CHECK(jelli_pet_init(&engine, platform, surface_for(pixels)));
    build_roster(&engine.game, JELLI_PET_CAPACITY, 6u);
    engine.game.active = 5u;
    CHECK(jelli_pet_frame(&engine));
    static const char request[] = "@J1 4 state\n";
    for (size_t i = 0u; request[i]; ++i)
        jelli_debug_feed(&debug, &engine, request[i], 0u);
    CHECK(debug.reply_size > 0u && debug.reply_size < JELLI_DEBUG_REPLY);
    CHECK(strstr(debug.reply, "\"active_slot\":5,\"pet_count\":8,\"stored_id\":16,"
                              "\"stored_form\":2,\"stored_asleep\":false") != NULL);
}

int main(void)
{
    every_form_and_state();
    ninja_assets_resolve();
    forms_over_days();
    switch_every_roster();
    debug_state_names_next_pet();
    puts("PASS: roster of 3-8 pets: every form/state frame, forms over days, SWITCH PET cycle");
    return 0;
}
