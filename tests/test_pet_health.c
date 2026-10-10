#include "game_fixture.h"
#include "jelli/pet_ui.h"
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
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];

static void check_unchanged(const JelliGame *before, const JelliGame *after)
{
    static uint8_t a[JELLI_SAVE_CAPACITY], b[JELLI_SAVE_CAPACITY];
    JelliSave first = {.game = *before}, second = {.game = *after};
    size_t size = jelli_save_encode(&first, a, sizeof(a));
    CHECK(size > 0u && jelli_save_encode(&second, b, sizeof(b)) == size);
    CHECK(memcmp(a, b, size) == 0);
}

static void press(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    JelliPetUiButton b;
    CHECK(jelli_pet_ui_control(ui, slot, game->pets[game->active].asleep, &b));
    jelli_pet_ui_tap(ui, game, (int)(b.bounds.x + b.bounds.width / 2u),
                     (int)(b.bounds.y + b.bounds.height / 2u));
}

static void test_clicker(void)
{
    for (unsigned kind = 0; kind < 5u; ++kind) {
        JelliGame g;
        JelliPetUi ui;
        test_game_pair(&g);
        jelli_pet_ui_init(&ui);
        JelliSurface s = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
        press(&ui, &g, 0u);
        press(&ui, &g, 1u); /* CARE */
        press(&ui, &g, 5u); /* HEALTH */
        CHECK(ui.page == JELLI_UI_HEALTH);
        press(&ui, &g, kind + 1u);
        CHECK(ui.page == (JelliPetPage)(JELLI_UI_BRUSH + kind));
        jelli_pet_render(&s, &g, &ui, 0u, false);
        JelliPetUiButton b;
        CHECK(jelli_pet_ui_control(&ui, 1u, false, &b));
        CHECK(b.bounds.y + b.bounds.height <= ui.actor_bounds.y);
        CHECK(!jelli_pet_ui_control(&ui, 2u, false, &b));
        uint16_t bond = g.pets[0].bond;
        jelli_pet_ui_tap(&ui, &g, 233, 300); /* Actor is not the click target. */
        CHECK(ui.clicker_hits == 0u);
        unsigned taps = 0u;
        while (!ui.clicker_done && taps < 36u) {
            press(&ui, &g, 1u);
            ++taps;
            CHECK(ui.result == JELLI_OK && ui.clicker_hits <= ui.clicker_goal);
            CHECK(g.pets[0].bond == bond + 5u * taps);
            jelli_pet_render(&s, &g, &ui, 0u, false);
            CHECK(ui.last_view.clicker_hits == ui.clicker_hits);
        }
        CHECK(ui.clicker_done);
        CHECK(kind == 0u   ? taps >= 6u && taps <= 10u
              : kind == 1u ? taps == 1u
              : kind == 2u ? taps >= 1u && taps <= 3u
                           : taps == JELLI_ACTIVITY_TAPS);
        JelliGame before = g;
        press(&ui, &g, 1u);
        CHECK(ui.result == JELLI_NOT_READY && ui.clicker_hits == ui.clicker_goal);
        check_unchanged(&before, &g);
        press(&ui, &g, 0u);
        CHECK(ui.page == JELLI_UI_HEALTH);
        press(&ui, &g, 0u);
        CHECK(ui.page == JELLI_UI_CARE);
        press(&ui, &g, 0u);
        CHECK(ui.page == JELLI_UI_HOME && ui.menu_open);
    }
}

static void test_cached_centroid(void)
{
    JelliGame g;
    JelliPetUi ui;
    test_game_pair(&g);
    jelli_pet_ui_init(&ui);
    JelliSurface s = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    ui.menu_open = true;
    for (unsigned form = 0; form < 2u; ++form) {
        for (unsigned pose = 0; pose < 6u; ++pose) {
            g.pets[0].form = (uint8_t)form;
            g.pets[0].asleep = pose == 4u;
            g.pets[0].nap_due = pose == 4u ? 36000u : 0u;
            g.pets[0].health = pose == 5u ? JELLI_UNWELL : JELLI_WELL;
            g.pets[0].activity = pose == 2u   ? JELLI_EATING
                                 : pose == 3u ? JELLI_PLAYING
                                              : JELLI_IDLE;
            jelli_pet_ui_init(&ui);
            ui.menu_open = true;
            jelli_pet_render(&s, &g, &ui, pose == 1u ? 900u : 0u, false);
            const JelliAsset *a = ui.actor_frame;
            CHECK(a == jelli_asset_find(1001u + form * 6u + pose));
            int x = ui.actor_x * 256 + (int)a->centroid_x_q8 * 6;
            int y = ui.actor_y * 256 + (int)a->centroid_y_q8 * 6;
            CHECK(abs(x - 233 * 256) <= 128 && abs(y - 233 * 256) <= 128);
            CHECK(ui.actor_bounds.height == (unsigned)(a->bottom - a->top) * 6u);
        }
    }
}

static void test_activity_reaction_priority(void)
{
    JelliGame game;
    JelliPetUi ui;
    test_game_pair(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    JelliPet *pet = &game.pets[0];
    for (unsigned form = 0u; form < 2u; ++form) {
        pet->form = (uint8_t)form;
        for (uint8_t reaction = 1u; reaction <= 3u; ++reaction) {
            pet->reaction = reaction;
            pet->reaction_ticks = 30u;
            pet->activity = JELLI_EATING;
            jelli_pet_render(&surface, &game, &ui, 0u, false);
            CHECK(ui.actor_frame == jelli_asset_find(1003u + form * 6u));
            pet->activity = JELLI_PLAYING;
            jelli_pet_render(&surface, &game, &ui, 0u, false);
            CHECK(ui.actor_frame == jelli_asset_find(1004u + form * 6u));
            pet->activity = JELLI_GIVING;
            jelli_pet_render(&surface, &game, &ui, 0u, false);
            CHECK(ui.actor_frame == jelli_asset_find(1004u + form * 6u));
            pet->activity = JELLI_IDLE;
            jelli_pet_render(&surface, &game, &ui, 0u, false);
            CHECK(ui.actor_frame == jelli_asset_find((reaction == 1u ? 1004u : 1006u) + form * 6u));
        }
    }
}

static void test_atmosphere_and_sound(void)
{
    JelliGame g;
    JelliPetUi ui;
    test_game_pair(&g);
    jelli_pet_ui_init(&ui);
    JelliSurface s = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    ui.clock_known = true;
    ui.clock_minute = 720u;
    jelli_pet_render(&s, &g, &ui, 0u, false);
    CHECK(ui.last_view.night == 0u);
    ui.clock_minute = 1260u;
    jelli_pet_render(&s, &g, &ui, 100u, false);
    CHECK(ui.last_view.night == 0u);
    jelli_pet_render(&s, &g, &ui, 6100u, false);
    CHECK(ui.last_view.night >= 126u && ui.last_view.night <= 128u);
    jelli_pet_render(&s, &g, &ui, 12100u, false);
    CHECK(ui.last_view.night == 255u && pixels[200u * JELLI_WIDTH + 100u] != 0u);
    g.pets[0].asleep = true;
    g.pets[0].nap_due = 36000u;
    jelli_pet_render(&s, &g, &ui, 12200u, false);
    CHECK(jelli_particles_count(&ui.particles) == 1u);
    const JelliParticle *z = &ui.particles.items[0];
    const JelliAsset *a = ui.actor_frame;
    int centroid = ui.actor_y + (int)((a->centroid_y_q8 * 6u + 128u) / 256u);
    CHECK(z->y / 16 == centroid - (int)ui.actor_bounds.height / 2);
    CHECK(z->style & 64u);
    jelli_particles_advance_scaled(&ui.particles, 60u, 60u);
    CHECK(ui.particles.items[0].vy == -22);
    g.pets[0].asleep = false;
    g.pets[0].nap_due = 0u;
    jelli_pet_render(&s, &g, &ui, 12300u, false);
    CHECK(jelli_particles_count(&ui.particles) == 0u);
    press(&ui, &g, 0u);
    CHECK(jelli_pet_ui_take_sound(&ui, 1u));
    CHECK(!jelli_pet_ui_take_sound(&ui, 2u));
    press(&ui, &g, 0u);
    CHECK(!jelli_pet_ui_take_sound(&ui, 100u));
    press(&ui, &g, 0u);
    CHECK(jelli_pet_ui_take_sound(&ui, 121u));
}

static void test_routine_tuning(void)
{
    JelliGame g;
    JelliPetUi ui;
    test_game_pair(&g);
    jelli_pet_ui_init(&ui);
    CHECK(jelli_tunable_set(&ui.tunables, 0u, JELLI_TUNE_BRUSH_MIN, 3u));
    CHECK(jelli_tunable_set(&ui.tunables, 0u, JELLI_TUNE_BRUSH_MAX, 3u));
    CHECK(jelli_tunable_set(&ui.tunables, 1u, JELLI_TUNE_FLOSS_MIN, 2u));
    CHECK(jelli_tunable_set(&ui.tunables, 1u, JELLI_TUNE_FLOSS_MAX, 2u));
    CHECK(jelli_pet_health_select(&ui, &g, JELLI_UI_ACTION_BRUSH));
    CHECK(ui.routine_goals[0] == 3u && ui.routine_goals[1] == 2u);
    CHECK(jelli_tunable_set(&ui.tunables, 0u, JELLI_TUNE_BRUSH_MAX, 6u));
    CHECK(ui.routine_goals[0] == 3u); /* Freeze targets during a round. */
    CHECK(jelli_pet_health_select(&ui, &g, JELLI_UI_ACTION_BRUSH));
    CHECK(ui.routine_goals[0] >= 3u && ui.routine_goals[0] <= 6u);
    CHECK(jelli_tunable_set(&ui.tunables, 0u, JELLI_TUNE_ACTIVITY_TAPS, 2u));
    CHECK(jelli_pet_health_select(&ui, &g, JELLI_UI_ACTION_WASH));
    CHECK(ui.clicker_goal == 2u);
}

static void test_idle_coos(void)
{
    JelliGame game;
    JelliPetUi ui;
    test_game_pair(&game);
    jelli_pet_ui_init(&ui);
    JelliPet *pet = &game.pets[0];
    CHECK(jelli_pet_ui_sound(&ui, pet, 0u) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, pet, 29999u) == 0u);
    CHECK(jelli_pet_ui_sound(&ui, pet, 30000u) == 7u);
    CHECK(jelli_pet_ui_sound(&ui, pet, 30001u) == 0u);
    ui.sound_pending = true;
    CHECK(jelli_pet_ui_sound(&ui, pet, 60000u) == 8u); /* Menu wins. */
    pet->asleep = true;
    CHECK(jelli_pet_ui_sound(&ui, pet, 90000u) == 0u);
    pet->asleep = false;
    CHECK(jelli_pet_ui_sound(&ui, pet, 119999u) == 0u);
    CHECK(jelli_tunable_set(&ui.tunables, pet->id, JELLI_TUNE_COO_MS, 10000u));
    CHECK(jelli_pet_ui_sound(&ui, pet, 120000u) == 7u);
    CHECK(jelli_tunable_set(&ui.tunables, pet->id, JELLI_TUNE_COO_ENABLED, 0u));
    CHECK(jelli_pet_ui_sound(&ui, pet, 500000u) == 0u);
    CHECK(jelli_tunable_set(&ui.tunables, pet->id, JELLI_TUNE_COO_ENABLED, 1u));
    CHECK(jelli_pet_ui_sound(&ui, pet, 1u) == 0u); /* Clock rewind reanchors. */
    ui.menu_open = true;
    CHECK(jelli_pet_ui_sound(&ui, pet, 100000u) == 0u);
}

static void test_health_cooldowns(void)
{
    JelliGame game;
    test_game_pair(&game);
    JelliPet *pet = &game.pets[0];
    JelliCommand shot = {JELLI_CMD_HEALTH, pet->id, 2u};
    unsigned goal = jelli_pet_shot_goal(pet);
    CHECK(goal >= 1u && goal <= 3u);
    for (unsigned i = 0; i < goal; ++i)
        CHECK(jelli_game_command(&game, shot) == JELLI_OK);
    CHECK(pet->shot_hits == goal && pet->shot_until == pet->ticks + 36000u);
    CHECK(!jelli_pet_health_ready(pet, 2u));
    JelliGame before = game;
    CHECK(jelli_game_command(&game, shot) == JELLI_NOT_READY);
    check_unchanged(&before, &game);
    JelliCommand medicine = {JELLI_CMD_HEALTH, pet->id, 1u};
    uint16_t social = pet->needs[JELLI_SOCIAL];
    CHECK(jelli_game_command(&game, medicine) == JELLI_OK);
    CHECK(pet->needs[JELLI_SOCIAL] == social + 10u);
    CHECK(jelli_game_command(&game, medicine) == JELLI_NOT_READY);
    CHECK(jelli_pet_health_ready(&game.pets[1], 1u));
    pet->ticks = pet->shot_until - 1u;
    CHECK(!jelli_pet_health_ready(pet, 2u));
    ++pet->ticks;
    CHECK(jelli_pet_health_ready(pet, 2u) && jelli_pet_health_ready(pet, 1u));
}

static unsigned bubble_count(const JelliPetUi *ui)
{
    unsigned count = 0u;
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i)
        if (ui->particles.items[i].life && (ui->particles.items[i].style & JELLI_PARTICLE_BUBBLE))
            ++count;
    return count;
}

static void test_routine_bubbles(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    ui.menu_open = true;
    ui.page = JELLI_UI_BRUSH;
    ui.clicker_pet = game.pets[0].id;
    jelli_pet_render(&surface, &game, &ui, 0u, false);
    CHECK(bubble_count(&ui) == 2u);
    int mouth_y = ui.actor_y + (int)((ui.actor_frame->centroid_y_q8 * 6u + 128u) / 256u) -
                  (int)ui.actor_bounds.height / 4;
    CHECK(ui.particles.items[0].y / 16 == mouth_y && ui.particles.items[0].vy < 0);
    jelli_pet_render(&surface, &game, &ui, 100u, false);
    CHECK(bubble_count(&ui) == 2u);
    jelli_pet_render(&surface, &game, &ui, 160u, false);
    CHECK(bubble_count(&ui) == 4u);
    ui.page = JELLI_UI_WASH;
    jelli_pet_render(&surface, &game, &ui, 200u, false);
    CHECK(bubble_count(&ui) == 4u);
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i) {
        const JelliParticle *particle = &ui.particles.items[i];
        if (!particle->life)
            continue;
        CHECK(particle->y / 16 == (int)ui.actor_bounds.y && particle->vy > 0);
        CHECK(particle->x / 16 >= (int)ui.actor_bounds.x);
        CHECK(particle->x / 16 < (int)(ui.actor_bounds.x + ui.actor_bounds.width));
    }
    jelli_particles_advance(&ui.particles, 20u);
    CHECK(ui.particles.items[4].y / 16 > (int)ui.actor_bounds.y);
    jelli_pet_render(&surface, &game, &ui, 100000u, false);
    CHECK(bubble_count(&ui) == 8u); /* A stall emits one batch, not a catch-up storm. */
    CHECK(surface.damage.x + surface.damage.width <= JELLI_WIDTH);
    CHECK(surface.damage.y + surface.damage.height <= JELLI_HEIGHT);
    ui.clicker_done = true;
    jelli_pet_render(&surface, &game, &ui, 100001u, false);
    CHECK(bubble_count(&ui) == 0u);
    ui.clicker_done = false;
    jelli_pet_render(&surface, &game, &ui, 100002u, false);
    CHECK(bubble_count(&ui) == 4u);
    ui.menu_open = false;
    jelli_pet_render(&surface, &game, &ui, 100003u, false);
    CHECK(bubble_count(&ui) == 0u);
}

int main(void)
{
    test_routine_bubbles();
    test_activity_reaction_priority();
    test_health_cooldowns();
    test_idle_coos();
    test_routine_tuning();
    test_atmosphere_and_sound();
    test_clicker();
    test_cached_centroid();
    puts(
        "Health UI input, game effects, completion, back hierarchy and cached centroids verified.");
    return 0;
}
