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
    CHECK(engine.ui.result == JELLI_NOT_READY && engine.ui.latched_prize == 6u);
    press(0u);
    press(6u);
    CHECK(engine.ui.page == JELLI_UI_SETTINGS);
    press(2u);
    CHECK(engine.game.active == 1u);
    press(0u);
    press(0u);
    CHECK(!engine.ui.menu_open && engine.ui.latched_prize == 6u);
    uint16_t before_bond = engine.game.pets[1].bond;
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

static FILE *open_snapshot(void)
{
#ifdef _MSC_VER
    FILE *file = NULL;
    return fopen_s(&file, "pet-gallery.ppm", "wb") == 0 ? file : NULL;
#else
    return fopen("pet-gallery.ppm", "wb");
#endif
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
    FILE *file = open_snapshot();
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

int main(void)
{
    gifts_open_all_nine_slots();
    catch_hold_switch_and_give();
    snapshot_grid();
    puts("PASS: nine-cell presents grid, catch, latch, cross-pet gift and snapshot");
    return 0;
}
