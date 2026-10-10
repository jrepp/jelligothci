#include "jelli/pet_engine.h"
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
static JelliPetEngine engine;
static uint64_t now;

static uint64_t clock_ms(void *context)
{
    (void)context;
    return now;
}

static void present(void *context, const JelliSurface *surface)
{
    (void)context;
    CHECK(surface->pixels == pixels);
}

static void frame(void)
{
    now += 1000u;
    CHECK(jelli_pet_frame(&engine));
    now += 1000u;
    CHECK(jelli_pet_frame(&engine));
}

static void reset(void)
{
    now = 0u;
    CHECK(jelli_pet_init(&engine, (JelliPlatform){.now_ms = clock_ms, .present = present},
                         (JelliSurface){pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}}));
    frame();
}

static void press(unsigned slot)
{
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&engine.ui, slot, engine.game.pets[engine.game.active].asleep,
                               &button));
    jelli_pet_ui_tap(&engine.ui, &engine.game, (int)(button.bounds.x + button.bounds.width / 2u),
                     (int)(button.bounds.y + button.bounds.height / 2u));
    frame();
}

static void gifts_open_all_nine_slots(void)
{
    reset();
    press(0u);
    press(5u);
    CHECK(engine.ui.page == JELLI_UI_COLLECTION && engine.ui.menu_open);
    for (unsigned slot = 1u; slot <= JELLI_PRIZE_COUNT; ++slot) {
        JelliPetUiButton button;
        CHECK(jelli_pet_ui_control(&engine.ui, slot, false, &button));
        CHECK(button.icon == 11000u + slot);
        CHECK(jelli_asset_find(button.icon) != NULL);
        CHECK(button.bounds.x >= 95u && button.bounds.y >= 88u);
        CHECK(engine.ui.last_view.unavailable & (1u << slot));
    }
    press(9u);
    CHECK(engine.ui.result == JELLI_NO_ITEM && engine.ui.page == JELLI_UI_COLLECTION);
    press(0u);
    CHECK(engine.ui.page == JELLI_UI_HOME && engine.ui.menu_open);
}

static void catch_hold_switch_and_give(void)
{
    reset();
    jelli_prize_complete(&engine.game, JELLI_PRIZE_WASH);
    frame();
    CHECK(engine.ui.last_view.offered_prize == 6u && !engine.ui.menu_open);
    uint8_t phase = engine.ui.last_view.catch_phase;
    now += 180u;
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.ui.last_view.catch_phase != phase);
    press(1u);
    CHECK(engine.game.prizes.owned == (1u << 5));
    CHECK(engine.ui.page == JELLI_UI_COLLECTION && engine.ui.highlighted_prize == 6u);
    CHECK(engine.ui.save_requested && engine.ui.save_status == JELLI_SAVE_PENDING);
    press(6u);
    CHECK(!engine.ui.menu_open && engine.ui.latched_prize == 6u);
    const JelliAsset *asset = jelli_asset_lookup(engine.ui.assets, 11006u);
    CHECK(asset != NULL);
    bool painted = false;
    for (unsigned row = 0u; row < asset->height && !painted; ++row)
        for (unsigned col = 0u; col < asset->width && !painted; ++col)
            if (asset->mask[row * asset->mask_stride + col / 8u] & (0x80u >> (col % 8u))) {
                CHECK(pixels[(78u + row * 2u) * JELLI_WIDTH + 201u + col * 2u] ==
                      asset->pixels[row * asset->width + col]);
                painted = true;
            }
    CHECK(painted);
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION && engine.ui.latched_prize == 6u);
    press(1u);
    CHECK(engine.ui.result == JELLI_NOT_READY && engine.ui.latched_prize == 6u);
    press(0u);
    press(0u);
    press(6u);
    CHECK(engine.ui.page == JELLI_UI_SETTINGS);
    press(2u);
    CHECK(engine.ui.page == JELLI_UI_PETS && engine.game.active == 0u);
    press(2u);
    CHECK(engine.ui.page == JELLI_UI_PET_DETAIL);
    press(1u);
    CHECK(engine.game.active == 1u);
    CHECK(!engine.ui.menu_open && engine.ui.latched_prize == 6u);
    uint16_t before_bond = engine.game.pets[1].bond;
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    press(1u);
    CHECK(engine.ui.result == JELLI_OK && !engine.ui.latched_prize);
    CHECK(!(engine.game.prizes.owned & (1u << 5)));
    CHECK(engine.game.prizes.discovered & (1u << 5));
    CHECK(engine.game.pets[1].bond == before_bond + 30u);
    CHECK(engine.game.prizes.offered == 9u);
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_COLLECTION && engine.ui.highlighted_prize == 9u);
    CHECK(engine.game.prizes.owned & (1u << 8));
}

static void put_away_and_browse(void)
{
    reset();
    jelli_prize_complete(&engine.game, JELLI_PRIZE_WASH);
    frame();
    press(1u);
    press(6u);
    uint16_t owned = engine.game.prizes.owned;
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    press(0u);
    CHECK(!engine.ui.menu_open && engine.ui.latched_prize == 6u);
    CHECK(jelli_game_command(&engine.game, (JelliCommand){JELLI_CMD_REST, 1u, 0u}) == JELLI_OK);
    frame();
    press(1u);
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    CHECK(jelli_pet_ui_available(&engine.ui, &engine.game, 2u) == JELLI_OK);
    press(2u);
    CHECK(!engine.ui.latched_prize && !engine.ui.menu_open);
    CHECK(engine.game.prizes.owned == owned);
    press(0u);
    press(5u);
    press(6u);
    press(0u);
    press(5u);
    press(6u); /* Selected cell opens the action panel. */
    CHECK(engine.ui.page == JELLI_UI_PRESENT_ACTION);
    press(0u);
    CHECK(engine.ui.page == JELLI_UI_COLLECTION && engine.ui.latched_prize == 6u);
    press(0u);
    press(6u);
    press(2u);
    CHECK(engine.ui.page == JELLI_UI_PETS && engine.game.active == 0u);
    press(9u);
    CHECK(engine.ui.page == JELLI_UI_PET_DETAIL);
    CHECK(jelli_pet_ui_available(&engine.ui, &engine.game, 1u) == JELLI_NOT_READY);
    press(2u);
    CHECK(engine.ui.page == JELLI_UI_EVOLUTIONS);
    press(2u);
    CHECK(engine.ui.selected_form == 1u && engine.game.active == 0u);
    press(0u);
    press(0u);
    press(2u);
    CHECK(jelli_pet_ui_available(&engine.ui, &engine.game, 1u) == JELLI_NOT_READY);
    CHECK(engine.game.sleep_log.active && engine.ui.latched_prize == 6u);
}

static FILE *open_snapshot(const char *path)
{
#ifdef _MSC_VER
    FILE *file = NULL;
    return fopen_s(&file, path, "wb") == 0 ? file : NULL;
#else
    return fopen(path, "wb");
#endif
}

static void snapshot(const char *path)
{
    FILE *file = open_snapshot(path);
    CHECK(file != NULL);
    CHECK(fprintf(file, "P6\n466 466\n255\n") > 0);
    for (unsigned i = 0u; i < JELLI_WIDTH * JELLI_HEIGHT; ++i) {
        uint16_t pixel = pixels[i];
        uint8_t rgb[3] = {(uint8_t)(((pixel >> 11) & 31u) * 255u / 31u),
                          (uint8_t)(((pixel >> 5) & 63u) * 255u / 63u),
                          (uint8_t)((pixel & 31u) * 255u / 31u)};
        CHECK(fwrite(rgb, 1u, sizeof(rgb), file) == sizeof(rgb));
    }
    CHECK(fclose(file) == 0);
}

static void snapshot_grid(void)
{
    reset();
    engine.game.prizes.owned = engine.game.prizes.discovered = JELLI_PRIZE_MASK;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        engine.game.prizes.origin_pet[i] = 1u;
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_COLLECTION;
    engine.ui.highlighted_prize = 9u;
    frame();
    CHECK(engine.ui.last_view.prize_owned == JELLI_PRIZE_MASK);
    snapshot("pet-gallery.ppm");
    engine.ui.page = JELLI_UI_PETS;
    frame();
    snapshot("pet-collection.ppm");
    press(3u);
    snapshot("pet-detail.ppm");
    press(2u);
    snapshot("pet-evolutions.ppm");
    engine.ui.page = JELLI_UI_PRESENT_ACTION;
    engine.ui.latched_prize = 6u;
    frame();
    snapshot("present-action.ppm");
}

int main(void)
{
    put_away_and_browse();
    gifts_open_all_nine_slots();
    catch_hold_switch_and_give();
    snapshot_grid();
    puts("PASS: nine-cell presents grid, catch, latch, cross-pet gift and snapshot");
    return 0;
}
