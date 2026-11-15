#include "jelli/pet_engine.h"
#include "jelli/invitations.h"
#include "jelli/activities.h"
#include "jelli/collectible.h"
#include "../core/pet_behavior_draw.h"
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
static JelliGame game;
static JelliPetUi ui;
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
static void init(void)
{
    jelli_game_init(&game);
    game.wall_known = true;
    game.wall_seconds = UINT64_C(10) * 3600u;
    jelli_pet_ui_init(&ui);
    jelli_pet_discovery_update(&ui, &game, 0u);
    jelli_pet_render(&surface, &game, &ui, 0u, false);
}
static void pointer(JelliInputKind kind, uint64_t now)
{
    jelli_pet_pointer(&ui, &game, (JelliInput){kind, 233, 200}, now);
}
static void quick_clean(void)
{
    init();
    game.pets[0].needs[JELLI_HYGIENE] = 200u;
    ui.last_view.stat_index = 3u;
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_DOWN, 230, 330}, 0u);
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_UP, 230, 330}, 100u);
    CHECK(ui.quick_menu && ui.menu_open && ui.page == JELLI_UI_MOMENTS);
    CHECK(ui.quick_count > 0u && ui.quick_count <= 4u);
    JelliPetUiButton button;
    CHECK(jelli_pet_quick_button(&ui, 1u, false, &button));
    CHECK(strcmp(button.label, "WASH") == 0);
    CHECK(jelli_pet_quick_available(&ui, &game, 1u) == JELLI_OK);
    jelli_pet_render(&surface, &game, &ui, 1000u, false);
    jelli_pet_render(&surface, &game, &ui, 2000u, false);
    jelli_pet_ui_tap(&ui, &game, (int)(button.bounds.x + 48u), (int)(button.bounds.y + 48u));
    CHECK(!ui.quick_menu && ui.page == JELLI_UI_WASH);
    init();
    game.pets[0].needs[JELLI_HYGIENE] = 200u;
    ui.last_view.stat_index = 3u;
    CHECK(jelli_pet_quick_open(&ui, &game, 230, 330));
    game.pets[0].asleep = true;
    CHECK(jelli_pet_quick_available(&ui, &game, 1u) != JELLI_OK);
}

static void gestures(void)
{
    init();
    CHECK(jelli_pet_actor_hit(&ui, &game, 233, 200));
    uint16_t bond = game.pets[0].bond;
    pointer(JELLI_POINTER_DOWN, 0u);
    CHECK(ui.hold_progress && !ui.menu_open);
    pointer(JELLI_POINTER_UP, 2999u);
    CHECK(ui.menu_open && ui.page == JELLI_UI_HOME);
    CHECK(game.pets[0].bond == bond && !game.pets[0].reaction);
    init();
    pointer(JELLI_POINTER_DOWN, 10u);
    jelli_pet_pointer_update(&ui, &game, 3009u);
    CHECK(!ui.menu_open);
    jelli_pet_pointer_update(&ui, &game, 3010u);
    CHECK(ui.menu_open && ui.page == JELLI_UI_SETTINGS);
    pointer(JELLI_POINTER_UP, 4000u);
    CHECK(ui.page == JELLI_UI_SETTINGS && game.pets[0].bond == bond);
    init();
    pointer(JELLI_POINTER_DOWN, 0u);
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_MOVE, 260, 200}, 100u);
    pointer(JELLI_POINTER_MOVE, 200u);
    pointer(JELLI_POINTER_UP, 4000u);
    CHECK(!ui.menu_open); /* Movement out and back cancels hold AND tap. */
    pointer(JELLI_POINTER_DOWN, 5000u);
    pointer(JELLI_POINTER_CANCEL, 5100u);
    pointer(JELLI_POINTER_UP, 9000u);
    CHECK(!ui.menu_open);
    game.pets[0].asleep = true;
    game.pets[0].nap_due = UINT64_MAX;
    pointer(JELLI_POINTER_DOWN, 10000u);
    pointer(JELLI_POINTER_UP, 10100u);
    CHECK(ui.menu_open && game.pets[0].asleep);
}
static void scene_target_priority(void)
{
    init();
    ui.actor_bounds = (JelliRect){190u, 65u, 100u, 100u};
    CHECK(jelli_pet_actor_hit(&ui, &game, 233, 110));
    game.prizes.offered = 1u;
    game.prizes.offered_pet = game.pets[0].id;
    CHECK(jelli_game_valid(&game));
    CHECK(!jelli_pet_actor_hit(&ui, &game, 233, 110));
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_DOWN, 233, 110}, 0u);
    jelli_pet_pointer_update(&ui, &game, 3000u);
    CHECK(!ui.menu_open && !ui.hold_progress);
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_UP, 233, 110}, 3100u);
    CHECK(ui.page == JELLI_UI_PRESENT_ACTION && !game.prizes.offered);
    CHECK(game.prizes.owned & 1u);

    init();
    JelliRect mess;
    CHECK(jelli_pet_mess_bounds(&ui, 1u, &mess));
    int x = (int)mess.x + 1, y = (int)mess.y + 1;
    CHECK(jelli_pet_actor_hit(&ui, &game, x, y));
    ui.last_view.mess = 1u;
    CHECK(!jelli_pet_actor_hit(&ui, &game, x, y));
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_DOWN, x, y}, 0u);
    jelli_pet_pointer(&ui, &game, (JelliInput){JELLI_POINTER_UP, x, y}, 100u);
    CHECK(game.pets[0].activity == JELLI_CLEANING && !ui.menu_open);
}

static void changing_pet_cancels_hold(void)
{
    init();
    test_game_pair(&game);
    pointer(JELLI_POINTER_DOWN, 0u);
    CHECK(ui.pointer.active && ui.pointer_pet_target);
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_ACTIVATE, 1u, 2u}) == JELLI_OK);
    jelli_pet_pointer_update(&ui, &game, 3000u);
    pointer(JELLI_POINTER_UP, 3100u);
    CHECK(!ui.menu_open && !ui.pointer.active && !ui.hold_progress);
}

static void paused_checkpoint_navigation(void)
{
    init();
    game.prizes.owned = game.prizes.discovered = UINT32_C(1) << JELLI_COLLECTIBLE_CHAIN_PRIZE;
    game.prizes.origin_pet[JELLI_COLLECTIBLE_CHAIN_PRIZE] = game.pets[0].id;
    CHECK(jelli_collectible_play(&game, JELLI_COLLECTIBLE_CHAIN_PRIZE) == JELLI_OK);
    CHECK(!jelli_pet_actor_hit(&ui, &game, 233, 200));
    CHECK(jelli_collectible_control(&game, JELLI_COLLECTIBLE_PAUSE) == JELLI_OK);
    CHECK(jelli_game_valid(&game));
    pointer(JELLI_POINTER_DOWN, 0u);
    pointer(JELLI_POINTER_UP, 100u);
    CHECK(ui.menu_open && ui.page == JELLI_UI_HOME && game.collectible.paused);
}

static void discovery(void)
{
    init();
    CHECK(ui.unseen_offers && ui.discovery_icon);
    CHECK(ui.last_view.discovery_icon);
    unsigned id = game.pets[0].invitations.leisure - 1u;
    pointer(JELLI_POINTER_DOWN, 10u);
    pointer(JELLI_POINTER_UP, 100u);
    jelli_pet_discovery_update(&ui, &game, 100u);
    CHECK(!game.pets[0].invitations.seen); /* Root isn't acknowledgement. */
    ui.page = JELLI_UI_MOMENTS;
    jelli_pet_discovery_update(&ui, &game, 200u);
    CHECK(ui.activity_snapshot && ui.activity_count > 0u);
    CHECK(ui.activity_count < jelli_moment_count + 2u);
    for (unsigned i = 0; i < ui.activity_count; ++i)
        CHECK(jelli_game_check(&game, jelli_pet_activity_command(&game, ui.activity_choices[i]),
                               &ui.action_scratch) == JELLI_OK);
    CHECK(ui.ready_offers & (UINT32_C(1) << jelli_pet_activity_choice(&ui, 1u)));
    ui.activity_page = 1u;
    jelli_pet_render(&surface, &game, &ui, 200u, false);
    jelli_pet_render(&surface, &game, &ui, 2000u, false);
    jelli_pet_discovery_update(&ui, &game, 2000u);
    CHECK(!(game.pets[0].invitations.seen & (UINT32_C(1) << id)));
    ui.activity_page = 0u;
    jelli_pet_render(&surface, &game, &ui, 2100u, false);
    jelli_pet_render(&surface, &game, &ui, 3000u, false);
    jelli_pet_discovery_update(&ui, &game, 3000u);
    CHECK(game.pets[0].invitations.seen & (UINT32_C(1) << id));
    uint8_t choices[34];
    memcpy(choices, ui.activity_choices, sizeof(choices));
    game.wall_seconds = game.pets[0].invitations.leisure_until * 60u;
    jelli_pet_discovery_update(&ui, &game, 3100u);
    CHECK(memcmp(choices, ui.activity_choices, sizeof(choices)) == 0);
    CHECK(!(game.pets[0].invitations.pending & (UINT32_C(1) << id)));
}
static void ready_only_and_play(void)
{
    init();
    ui.menu_open = true;
    ui.page = JELLI_UI_MOMENTS;
    jelli_pet_discovery_update(&ui, &game, 10u);
    unsigned play = ui.activity_count;
    for (unsigned i = 0; i < ui.activity_count; ++i)
        if (ui.activity_choices[i] == jelli_moment_count + 1u)
            play = i;
    CHECK(play < ui.activity_count);
    ui.activity_page = (uint8_t)(play / 5u);
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&ui, play % 5u + 1u, false, &button));
    CHECK(strcmp(button.label, "PLAY") == 0);
    jelli_pet_ui_tap(&ui, &game, (int)button.bounds.x + 48, (int)button.bounds.y + 48);
    CHECK(game.pets[0].activity == JELLI_PLAYING && !ui.menu_open);
    init();
    game.pets[0].asleep = true;
    game.pets[0].nap_due = UINT64_MAX;
    ui.menu_open = true;
    ui.page = JELLI_UI_MOMENTS;
    jelli_pet_discovery_update(&ui, &game, 10u);
    CHECK(ui.activity_snapshot && !ui.activity_count && jelli_pet_activity_pages(&ui) == 1u);
    for (unsigned slot = 1; slot <= 6u; ++slot)
        CHECK(!jelli_pet_ui_control(&ui, slot, true, &button));
    CHECK(jelli_pet_ui_control(&ui, 0u, true, &button));
}

static void home_discovery(void)
{
    init();
    pointer(JELLI_POINTER_DOWN, 10u);
    pointer(JELLI_POINTER_UP, 100u);
    jelli_pet_discovery_update(&ui, &game, 100u);
    JelliPetUiButton button;
    const char *labels[] = {"FOOD", "WATER", "CARE"};
    for (unsigned slot = 2u; slot <= 4u; ++slot) {
        CHECK(jelli_pet_ui_control(&ui, slot, false, &button));
        CHECK(strcmp(button.label, labels[slot - 2u]) == 0);
        CHECK(button.bounds.x > JELLI_WIDTH / 2u);
    }
    ui.save_requested = false;
    uint16_t bond = game.pets[0].bond;
    uint8_t health = game.pets[0].health;
    CHECK(jelli_pet_ui_control(&ui, 4u, false, &button));
    jelli_pet_ui_tap(&ui, &game, (int)button.bounds.x + 48, (int)button.bounds.y + 48);
    CHECK(ui.page == JELLI_UI_HEALTH && ui.menu_open);
    CHECK(game.pets[0].health == health && game.pets[0].bond == bond);
    CHECK(game.pets[0].activity == JELLI_IDLE && !ui.save_requested);
    jelli_pet_ui_back(&ui);
    CHECK(ui.page == JELLI_UI_HOME);
    jelli_pet_discovery_refresh(&ui, &game, 100u);
    CHECK(jelli_pet_ui_control(&ui, 5u, false, &button));
    CHECK(strcmp(button.label, "ACTIVITIES") == 0 && button.bounds.x < JELLI_WIDTH / 2u);
    unsigned choice = jelli_pet_home_choice(&ui, 1u);
    CHECK(choice < jelli_moment_count && (ui.ready_offers & (UINT32_C(1) << choice)));
    CHECK(jelli_pet_ui_control(&ui, 1u, false, &button));
    CHECK(button.bounds.x < JELLI_WIDTH / 2u);
    jelli_pet_ui_tap(&ui, &game, (int)button.bounds.x + 48, (int)button.bounds.y + 48);
    CHECK(ui.result == JELLI_OK && !ui.menu_open && game.pets[0].activity != JELLI_IDLE);
    init();
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(ui.menu_open && ui.page == JELLI_UI_SETTINGS && !game.pets[0].asleep);
}

static uint64_t fake_now;
static JelliInput fake_input;
static bool fake_pending;
static JelliPetEngine engine;
static uint64_t now_ms(void *ctx)
{
    (void)ctx;
    return fake_now;
}
static bool poll(void *ctx, JelliInput *input)
{
    (void)ctx;
    if (!fake_pending)
        return false;
    *input = fake_input;
    fake_pending = false;
    return true;
}
static void present(void *ctx, const JelliSurface *frame)
{
    (void)ctx;
    (void)frame;
}
static void engine_hold(void)
{
    fake_now = 0u;
    CHECK(jelli_pet_init(&engine, (JelliPlatform){NULL, now_ms, poll, present, NULL}, surface));
    CHECK(jelli_pet_frame(&engine));
    fake_input = (JelliInput){JELLI_POINTER_DOWN, 233, 200};
    fake_pending = true;
    CHECK(jelli_pet_frame(&engine));
    fake_now = 2999u;
    CHECK(jelli_pet_frame(&engine) && !engine.ui.menu_open);
    fake_now = 3000u;
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.ui.menu_open && engine.ui.page == JELLI_UI_SETTINGS);
    fake_input = (JelliInput){JELLI_POINTER_UP, 233, 200};
    fake_pending = true;
    CHECK(jelli_pet_frame(&engine) && engine.ui.page == JELLI_UI_SETTINGS);
}

static void capture(const char *path)
{
    FILE *f = fopen(path, "wb");
    CHECK(f);
    CHECK(fprintf(f, "P6\n466 466\n255\n") > 0);
    for (unsigned i = 0; i < JELLI_WIDTH * JELLI_HEIGHT; ++i) {
        uint16_t p = pixels[i];
        const unsigned char rgb[3] = {(unsigned char)((p >> 11) * 255u / 31u),
                                      (unsigned char)(((p >> 5) & 63u) * 255u / 63u),
                                      (unsigned char)((p & 31u) * 255u / 31u)};
        CHECK(fwrite(rgb, 1, 3, f) == 3u);
    }
    CHECK(fclose(f) == 0);
}
int main(void)
{
    quick_clean();
    scene_target_priority();
    changing_pet_cancels_hold();
    home_discovery();
    ready_only_and_play();
    gestures();
    discovery();
    paused_checkpoint_navigation();
    init();
    capture("rfc013-discovery.ppm");
    ui.menu_open = true;
    jelli_pet_discovery_update(&ui, &game, 1000u);
    jelli_pet_render(&surface, &game, &ui, 1000u, false);
    jelli_pet_render(&surface, &game, &ui, 2000u, false);
    capture("rfc013-wheel.ppm");
    ui.page = JELLI_UI_MOMENTS;
    jelli_pet_discovery_update(&ui, &game, 3000u);
    jelli_pet_render(&surface, &game, &ui, 3000u, false);
    jelli_pet_render(&surface, &game, &ui, 4000u, false);
    capture("rfc013-activities.ppm");
    init();
    pointer(JELLI_POINTER_DOWN, 0u);
    jelli_pet_pointer_update(&ui, &game, 1500u);
    jelli_pet_render(&surface, &game, &ui, 1500u, false);
    capture("rfc013-hold.ppm");
    engine_hold();
    puts("PASS: pet gestures, invitation cues, acknowledgement and stable targets");
    return 0;
}
