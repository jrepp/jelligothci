#ifndef JELLI_TEST_UI_NAVIGATION_FIXTURE_H
#define JELLI_TEST_UI_NAVIGATION_FIXTURE_H
/* Render-suite navigation scenarios; CHECK is supplied by the including test. */
static void tap_item(JelliPetUi *ui, JelliGame *game, unsigned item)
{
    if (!ui->menu_open)
        jelli_pet_ui_swipe(ui, game, 0, -80);
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(ui, item + 1u, game->pets[game->active].asleep, &button));
    jelli_pet_ui_tap(ui, game, (int)(button.bounds.x + 48u), (int)(button.bounds.y + 48u));
}

static void open_pet_menu(JelliPetUi *ui, JelliGame *game)
{
    for (unsigned i = 0u; i < 8u && ui->menu_open; ++i)
        jelli_pet_ui_back(ui);
    CHECK(!ui->menu_open);
    jelli_pet_ui_swipe(ui, game, 0, 80);
    CHECK(ui->page == JELLI_UI_SETTINGS);
    tap_item(ui, game, 1u);
}

static void test_navigation_and_actions(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    open_pet_menu(&ui, &game);
    CHECK(ui.page == JELLI_UI_MORE);
    tap_item(&ui, &game, 0u);
    CHECK(game.pets[game.active].activity == JELLI_CLEANING);
    for (unsigned step = 0u; step < 10u; ++step)
        jelli_game_advance(&game, 1000u);
    CHECK(game.pets[game.active].activity == JELLI_IDLE);
    jelli_pet_ui_swipe(&ui, &game, 0, -80);
    CHECK(ui.page == JELLI_UI_HOME);
    open_pet_menu(&ui, &game);
    tap_item(&ui, &game, 3u);
    CHECK(ui.page == JELLI_UI_COLLECTION);
    CHECK(game.pets[game.active].activity == JELLI_IDLE);
    CHECK(ui.save_requested);

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    open_pet_menu(&ui, &game);
    tap_item(&ui, &game, 2u);
    CHECK(ui.page == JELLI_UI_PETS);
    CHECK(game.active == 0u);
    CHECK(!ui.save_requested);

    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    open_pet_menu(&ui, &game);
    tap_item(&ui, &game, 4u);
    CHECK(ui.page == JELLI_UI_SETTINGS);
    uint32_t bedtime = game.pets[game.active].bedtime;
    tap_item(&ui, &game, 0u);
    CHECK(game.pets[game.active].bedtime == (bedtime + 1u) % 24u);
    CHECK(ui.save_status == JELLI_SAVE_PENDING && ui.save_requested);

    game.resuming = true;
    game.resume_remaining_ms = 60000u;
    uint8_t page = (uint8_t)ui.page;
    jelli_pet_ui_tap(&ui, &game, 233, 420);
    CHECK((uint8_t)ui.page == page);
    CHECK(game.pets[game.active].bedtime == (bedtime + 1u) % 24u);
}

static void tap_moment(JelliPetUi *ui, JelliGame *game, unsigned id)
{
    for (unsigned i = 0; i < ui->activity_count; ++i) {
        if (ui->activity_choices[i] == id) {
            ui->activity_page = (uint8_t)(i / 5u);
            tap_item(ui, game, i % 5u);
            return;
        }
    }
    CHECK(false);
}

#endif
