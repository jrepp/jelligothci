#include "collection_catalog_fixture.h"
#include "jelli/pet_engine.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
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

static void press(unsigned slot)
{
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&engine.ui, slot, false, &button));
    jelli_pet_ui_tap(&engine.ui, &engine.game, (int)(button.bounds.x + button.bounds.width / 2u),
                     (int)(button.bounds.y + button.bounds.height / 2u));
    frame();
}

int main(void)
{
    CHECK(jelli_pet_init(&engine, (JelliPlatform){.now_ms = clock_ms, .present = present},
                         (JelliSurface){pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}}));
    for (unsigned i = 0u; i < jelli_prize_count; ++i) {
        jelli_prize_award(&engine.game, i);
        CHECK(jelli_prize_catch(&engine.game) == JELLI_OK);
    }
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_PETS;
    frame();
    frame();
    for (unsigned slot = 1u; slot <= 16u; ++slot) {
        JelliPetUiButton button;
        CHECK(jelli_pet_ui_control(&engine.ui, slot, false, &button));
        const JelliAsset *asset = jelli_asset_find(button.icon);
        CHECK(asset && asset->width == 48u);
        CHECK(asset->width * button.scale <= button.bounds.width - 6u);
        CHECK(asset->height * button.scale <= button.bounds.height - 6u);
        CHECK(button.bounds.x + button.bounds.width <= JELLI_WIDTH);
        CHECK(button.bounds.y + button.bounds.height < 376u);
    }
    CHECK(engine.ui.last_view.pets_owned == UINT16_MAX);
    CHECK(engine.ui.last_view.pets_new & UINT16_C(0x8000));
    press(16u);
    CHECK(engine.ui.page == JELLI_UI_PET_DETAIL && engine.ui.selected_pet == 16u);
    CHECK((engine.game.new_pets & UINT16_C(0x8000)) == 0u);
    press(1u);
    CHECK(engine.game.pets[engine.game.active].collection_entry == 16u);
    puts("PASS: sixteen-family rendering, 48px compact icon fit and last-family navigation");
    return 0;
}
