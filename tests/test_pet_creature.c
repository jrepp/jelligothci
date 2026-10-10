#include "jelli/assets.h"
#include "jelli/collection.h"
#include "jelli/creature.h"
#include "jelli/pet_ui.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];

static uint32_t frame_at(const JelliGame *game, JelliPetUi *ui, uint64_t ms)
{
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    jelli_pet_render(&surface, game, ui, ms, false);
    CHECK(ui->actor_frame != NULL);
    return ui->actor_frame->id;
}

/* BUBBLE (entry 3) is the axolotl; make it the active pet. */
static void axolotl_game(JelliGame *game)
{
    jelli_game_init(game);
    game->prizes.offered = 6u;
    game->prizes.offered_pet = game->pets[0].id;
    CHECK(jelli_prize_catch(game) == JELLI_OK);
    int index = jelli_collection_find(game, 3u);
    CHECK(index > 0 && game->pets[index].form == 2u);
    CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_ACTIVATE, game->pets[0].id,
                                                  game->pets[index].id}) == JELLI_OK);
    CHECK(game->pets[game->active].collection_entry == 3u);
}

static void every_form_has_grounded_clips(void)
{
    for (unsigned form = 0u; form < jelli_collection_form_count; ++form) {
        CHECK(jelli_creature_clip(form, JELLI_POSE_IDLE)->frames[0] ==
              jelli_collection_forms[form].portrait);
        for (unsigned pose = 0u; pose < JELLI_POSE_COUNT; ++pose) {
            const JelliClip *clip = jelli_creature_clip(form, pose);
            CHECK(clip && clip->count >= 1u && clip->count <= JELLI_CLIP_FRAME_CAPACITY);
            for (unsigned i = 0u; i < clip->count; ++i) {
                const JelliAsset *asset = jelli_asset_find(clip->frames[i]);
                CHECK(asset && asset->ground_y_q8 && clip->durations_ms[i]);
            }
        }
    }
    CHECK(jelli_creature_clip(jelli_collection_form_count, 0u) == NULL);
    CHECK(jelli_creature_clip(0u, JELLI_POSE_COUNT) == NULL);
}

static void axolotl_idles_and_blinks(void)
{
    JelliGame game;
    JelliPetUi ui;
    axolotl_game(&game);
    jelli_pet_ui_init(&ui);
    /* 900 ms idle beats: idle, blink (idle-alt), then two idle beats in a row. */
    CHECK(frame_at(&game, &ui, 0u) == 1101u);
    CHECK(frame_at(&game, &ui, 450u) == 1102u);
    CHECK(frame_at(&game, &ui, 900u) == 1101u);
    CHECK(frame_at(&game, &ui, 990u) == 1105u);
    CHECK(frame_at(&game, &ui, 1080u) == 1106u);
    CHECK(frame_at(&game, &ui, 1800u) == 1101u);
    CHECK(frame_at(&game, &ui, 2250u) == 1102u);
    CHECK(frame_at(&game, &ui, 2700u) == 1103u);
    CHECK(frame_at(&game, &ui, 3150u) == 1104u);
}

static void axolotl_sleep_breathes_and_frame_changes_redraw(void)
{
    JelliGame game;
    JelliPetUi ui;
    axolotl_game(&game);
    jelli_pet_ui_init(&ui);
    CHECK(jelli_game_command(
              &game, (JelliCommand){JELLI_CMD_REST, game.pets[game.active].id, 0u}) == JELLI_OK);
    CHECK(game.pets[game.active].asleep);
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    CHECK(frame_at(&game, &ui, 0u) == 1110u);
    jelli_pet_render(&surface, &game, &ui, 100u, false);
    CHECK(ui.actor_frame->id == 1110u && surface.damage.width == 0u); /* Same frame: no damage. */
    jelli_pet_render(&surface, &game, &ui, 900u, false);
    CHECK(ui.actor_frame->id == 1111u && surface.damage.width != 0u);
    CHECK(frame_at(&game, &ui, 1800u) == 1110u);
    jelli_pet_render(&surface, &game, &ui, 2700u, true); /* Paused: hold the shown frame. */
    CHECK(ui.actor_frame->id == 1110u);
}

static void axolotl_actor_sits_on_the_floor(void)
{
    JelliGame game;
    JelliPetUi ui;
    axolotl_game(&game);
    jelli_pet_ui_init(&ui);
    (void)frame_at(&game, &ui, 0u);
    const JelliAsset *a = ui.actor_frame;
    CHECK(a->width == 48u && a->height == 48u);
    CHECK(ui.actor_y + (int)a->bottom * 6 == 256); /* Contact row matches 32x32 forms. */
    CHECK(ui.actor_bounds.width == (unsigned)(a->right - a->left) * 6u);
}

int main(void)
{
    every_form_has_grounded_clips();
    axolotl_idles_and_blinks();
    axolotl_sleep_breathes_and_frame_changes_redraw();
    axolotl_actor_sits_on_the_floor();
    puts("PASS: data-driven creature clips animate, redraw, pause, and ground every form");
    return 0;
}
