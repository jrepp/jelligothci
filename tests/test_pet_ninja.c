/* The ninja pet on screen: per-form frame selection, its home screen, and SWITCH PET
 * cycling through all three pets with the Settings ring describing the next one. */
#include "jelli/assets.h"
#include "jelli/pet_ui.h"
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

enum { IDLE_A, IDLE_B, EATING, HAPPY, ASLEEP, UNWELL, CURIOUS, CONTENT };

static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static uint16_t mint_pixels[JELLI_WIDTH * JELLI_HEIGHT];

/* Expected asset IDs, written independently of core/pet_actor.c from assets.json. */
static uint32_t expected_id(unsigned form, unsigned pose)
{
    if (form == 2u)
        return 1031u + pose;
    if (form > 2u)
        form = 0u; /* Unknown forms fall back to Mint. */
    return pose >= CURIOUS ? 1021u + form * 2u + (pose - CURIOUS) : 1001u + form * 6u + pose;
}

typedef struct {
    bool asleep;
    uint8_t health, activity, reaction, phase;
    unsigned pose;
} FrameCase;

static void frame_table(void)
{
    static const FrameCase cases[] = {
        {false, JELLI_WELL, JELLI_IDLE, 0u, 0u, IDLE_A},
        {false, JELLI_WELL, JELLI_IDLE, 0u, 1u, IDLE_B},
        {false, JELLI_WELL, JELLI_IDLE, 0u, 2u, CURIOUS},
        {false, JELLI_WELL, JELLI_IDLE, 0u, 3u, CONTENT},
        {false, JELLI_WELL, JELLI_EATING, 0u, 0u, EATING},
        {false, JELLI_WELL, JELLI_EATING, 3u, 0u, EATING}, /* Care beats a touch reaction. */
        {false, JELLI_WELL, JELLI_PLAYING, 0u, 0u, HAPPY},
        {false, JELLI_WELL, JELLI_GIVING, 2u, 0u, HAPPY},
        {false, JELLI_WELL, JELLI_CLEANING, 0u, 1u, IDLE_B},
        {false, JELLI_WELL, JELLI_IDLE, 1u, 3u, HAPPY},
        {false, JELLI_WELL, JELLI_IDLE, 2u, 2u, UNWELL},
        {false, JELLI_WELL, JELLI_IDLE, 3u, 0u, UNWELL},
        {false, JELLI_UNWELL, JELLI_EATING, 0u, 0u, UNWELL},
        {false, JELLI_RECOVERING, JELLI_CARING, 1u, 0u, UNWELL},
        {true, JELLI_UNWELL, JELLI_IDLE, 0u, 0u, ASLEEP},
        {true, JELLI_WELL, JELLI_IDLE, 1u, 2u, ASLEEP},
    };
    static const uint8_t forms[] = {0u, 1u, 2u, 3u, 200u, 255u};
    for (unsigned f = 0u; f < sizeof(forms); ++f) {
        for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            const FrameCase *c = &cases[i];
            JelliPetUi ui;
            jelli_pet_ui_init(&ui);
            JelliPetRenderKey view = {.form = forms[f],
                                      .asleep = c->asleep,
                                      .health = c->health,
                                      .activity = c->activity,
                                      .reaction = c->reaction,
                                      .phase = c->phase};
            jelli_pet_actor_layout(&ui, &view);
            uint32_t id = expected_id(forms[f], c->pose);
            CHECK(ui.actor_frame != NULL && ui.actor_frame->id == id);
            CHECK(jelli_asset_find(id) == ui.actor_frame);
        }
    }
}

static JelliSurface surface(uint16_t *buffer)
{
    return (JelliSurface){buffer, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
}

static bool rows_differ(const uint16_t *a, const uint16_t *b, size_t top, size_t bottom)
{
    size_t offset = top * (size_t)JELLI_WIDTH;
    size_t bytes = (bottom - top) * (size_t)JELLI_WIDTH * sizeof(uint16_t);
    return memcmp(a + offset, b + offset, bytes) != 0;
}

/* Renders the ninja's home screen in each reachable pose with real game state. */
static void ninja_home_screen(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface mint = surface(mint_pixels);
    jelli_pet_render(&mint, &game, &ui, 0u, false);
    CHECK(ui.actor_frame && ui.actor_frame->id == 1001u);
    game.active = 2u;
    JelliPet *ninja = &game.pets[2];
    JelliSurface s = surface(pixels);
    jelli_pet_render(&s, &game, &ui, 0u, false);
    CHECK(ui.actor_frame && ui.actor_frame->id == 1031u && ui.last_view.form == JELLI_FORM_NINJA);
    CHECK(s.damage.width == JELLI_WIDTH && s.damage.height == JELLI_HEIGHT);
    CHECK(rows_differ(pixels, mint_pixels, 16u, 52u));  /* NINJA, not MINT, in the heading. */
    CHECK(!rows_differ(pixels, mint_pixels, 57u, 81u)); /* Same @ HOME line. */
    /* The 6x sprite stays below the heading lines and inside the panel. */
    CHECK(ui.actor_bounds.y >= 81u && ui.actor_bounds.y + ui.actor_bounds.height <= 282u);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, ninja->id, 0u}) == JELLI_OK);
    jelli_pet_render(&s, &game, &ui, 0u, false);
    CHECK(ui.actor_frame->id == 1033u);
    ninja->activity = JELLI_PLAYING;
    jelli_pet_render(&s, &game, &ui, 0u, false);
    CHECK(ui.actor_frame->id == 1034u);
    ninja->activity = JELLI_IDLE;
    ninja->interaction_due = 0u;
    ninja->health = JELLI_UNWELL;
    jelli_pet_render(&s, &game, &ui, 0u, false);
    CHECK(ui.actor_frame->id == 1036u);
    ninja->health = JELLI_WELL;
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, ninja->id, 0u}) == JELLI_OK);
    jelli_pet_render(&s, &game, &ui, 0u, false);
    CHECK(ui.actor_frame->id == 1035u && jelli_game_valid(&game));
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

static void switch_pet_cycles(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.pets[2].asleep = true; /* A resting ninja is still a valid switch target. */
    game.pets[2].nap_due = UINT64_MAX;
    JelliSurface s = surface(pixels);
    static const uint32_t order[] = {1u, 2u, 3u, 1u};
    for (unsigned i = 0u; i + 1u < sizeof(order) / sizeof(order[0]); ++i) {
        ui.menu_open = true;
        ui.page = JELLI_UI_SETTINGS;
        jelli_pet_render(&s, &game, &ui, 0u, false);
        const JelliPet *next = &game.pets[(game.active + 1u) % game.count];
        CHECK(ui.last_view.active_id == order[i] && ui.last_view.stored_id == order[i + 1u]);
        CHECK(ui.last_view.stored_form == next->form && ui.last_view.stored_asleep == next->asleep);
        CHECK(ui.last_view.count == 3u);
        CHECK(jelli_pet_ui_available(&ui, &game, 2u) == JELLI_OK);
        tap_switch(&ui, &game);
        CHECK(ui.result == JELLI_OK && game.pets[game.active].id == order[i + 1u]);
        CHECK(ui.save_requested && jelli_game_valid(&game));
    }
    /* A busy pet keeps the slot and reports NOT READY, as before. */
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, 1u, 0u}) == JELLI_OK);
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    CHECK(jelli_pet_ui_available(&ui, &game, 2u) == JELLI_NOT_READY);
    tap_switch(&ui, &game);
    CHECK(ui.result == JELLI_NOT_READY && game.active == 0u);
}

int main(void)
{
    frame_table();
    ninja_home_screen();
    switch_pet_cycles();
    puts("PASS: ninja frames for every form/state, home screen, SWITCH PET cycle and preview");
    return 0;
}
