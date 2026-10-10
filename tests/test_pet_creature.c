#include "jelli/assets.h"
#include <string.h>
#include "jelli/potty.h"
#include "jelli/behavior.h"
#include "jelli/collection.h"
#include "jelli/creature.h"
#include "jelli/pet_ui.h"
#include "../core/pet_behavior_draw.h"

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
    CHECK(jelli_creature_clip(0u, jelli_creature_pose_count) == NULL);
}

static void axolotl_idles_and_blinks(void)
{
    JelliGame game;
    JelliPetUi ui;
    axolotl_game(&game);
    jelli_pet_ui_init(&ui);
    /* 900 ms beats from the axolotl profile: idle, idle (one continuous 4-frame bob), blink. */
    CHECK(frame_at(&game, &ui, 0u) == 1101u);
    CHECK(frame_at(&game, &ui, 450u) == 1102u);
    CHECK(frame_at(&game, &ui, 900u) == 1103u);
    CHECK(frame_at(&game, &ui, 1350u) == 1104u);
    CHECK(frame_at(&game, &ui, 1800u) == 1101u); /* idle-alt: blink starts open... */
    CHECK(frame_at(&game, &ui, 1890u) == 1105u);
    CHECK(frame_at(&game, &ui, 1980u) == 1105u); /* Half blink holds; blink-2 was retired. */
    CHECK(frame_at(&game, &ui, 2160u) == 1101u); /* ...and holds open after one blink. */
    CHECK(frame_at(&game, &ui, 2700u) == 1101u); /* Back to idle: the bob restarts. */
    CHECK(frame_at(&game, &ui, 3150u) == 1102u);
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
    unsigned scale = jelli_creature_profile(2u)->scale;
    CHECK(ui.actor_scale == scale && scale != jelli_creature_profile(0u)->scale);
    CHECK(ui.actor_y + (int)(a->bottom * scale) == 256); /* Same contact row as 32x32 forms. */
    CHECK(ui.actor_bounds.width == (unsigned)(a->right - a->left) * scale);
}

/* The jelly behaviour in content/creatures.json reproduces the former code tables. */
static void jelly_profile_matches_legacy_rules(void)
{
    static const uint8_t legacy[] = {0, 1, 0, 0, 2, 0, 1, 1, 0, 3, 3, 0, 0, 1, 0, 0};
    for (unsigned form = 0u; form < 2u; ++form) {
        const JelliCreatureProfile *p = jelli_creature_profile(form);
        CHECK(p->scale == 6u && p->icon_scale == 2u && p->portrait_scale == 4u);
        for (uint32_t id = 1u; id <= 3u; ++id)
            for (uint64_t beat = 0u; beat < 160u; ++beat) {
                unsigned pose = legacy[beat % 16u];
                if (pose >= 2u && ((beat / 16u + id) % 3u) == 0u)
                    pose = 0u;
                CHECK(jelli_creature_idle_pose(p, beat, id) == pose);
            }
    }
    CHECK(jelli_creature_profile(99u) == jelli_creature_profile(0u));
}

static void behaviour_states_show_their_look(void)
{
    JelliGame game;
    JelliPetUi ui;
    axolotl_game(&game);
    jelli_pet_ui_init(&ui);
    JelliPet *pet = &game.pets[game.active];
    for (unsigned state = 0u; state < jelli_behavior_state_count; ++state) {
        const JelliBehaviorLook *look = jelli_behavior_look(state);
        CHECK(look && look->pose < jelli_creature_pose_count && look->caption[0]);
        CHECK(!look->effect || jelli_asset_find(look->effect));
        CHECK(!look->prop || jelli_asset_find(look->prop));
        pet->behavior = (uint8_t)(state + 1u);
        pet->behavior_left = 10u;
        (void)frame_at(&game, &ui, 10000u + state * 1000u);
        CHECK(ui.last_view.pose == look->pose && ui.last_view.behavior == state + 1u);
        const JelliClip *clip = jelli_creature_clip(pet->form, look->pose);
        CHECK(clip && ui.actor_frame->id == clip->frames[ui.last_view.clip_frame]);
    }
    /* The axolotl's own potty clip, not the unwell fallback, plays while it needs to go. */
    for (unsigned state = 0u; state < jelli_behavior_state_count; ++state)
        if (strcmp(jelli_behavior_states[state].name, "asking_potty") == 0)
            CHECK(jelli_creature_clip(pet->form, jelli_behavior_look(state)->pose)->frames[0] ==
                  1115u);
    CHECK(jelli_creature_clip(0u, jelli_behavior_look(0u)->pose) != NULL); /* Mint falls back. */
}

static void mess_shimmers_on_the_floor(void)
{
    JelliGame game;
    JelliPetUi ui;
    axolotl_game(&game);
    jelli_pet_ui_init(&ui);
    game.pets[game.active].behavior_flags |= JELLI_PET_FLAG_MESS;
    (void)frame_at(&game, &ui, 0u);
    CHECK(ui.last_view.mess == 1u);
    (void)frame_at(&game, &ui, jelli_potty_rules.mess_frame_ms);
    CHECK(ui.last_view.mess == 2u);
    for (unsigned i = 0u; i < jelli_potty_rules.mess_sprite_count; ++i)
        CHECK(jelli_asset_find(jelli_potty_rules.mess_sprites[i]) != NULL);
    /* A mess left out costs mood; tapping it starts the same clean-up as Care > Clean. */
    JelliPet *pet = &game.pets[game.active];
    unsigned messy = jelli_pet_mood(pet);
    jelli_potty_clean(pet);
    CHECK(jelli_pet_mood(pet) > messy);
    pet->behavior_flags |= JELLI_PET_FLAG_MESS;
    (void)frame_at(&game, &ui, 0u);
    JelliRect mess;
    CHECK(jelli_pet_mess_bounds(&ui, ui.last_view.mess, &mess));
    CHECK(mess.width == 16u * ui.actor_scale); /* Drawn at the pet's pixel scale. */
    CHECK(mess.y + mess.height <= 256u + ui.actor_scale && mess.x >= ui.actor_bounds.x);
    jelli_pet_ui_tap(&ui, &game, (int)(mess.x + mess.width / 2u), (int)(mess.y + mess.height / 2u));
    CHECK(ui.result == JELLI_OK && pet->activity == JELLI_CLEANING);
    (void)frame_at(&game, &ui, 1000u);
    unsigned first = ui.last_view.sweep;
    CHECK(first == 1u); /* The broom appears as the clean starts... */
    jelli_game_advance(&game, 500u);
    jelli_game_advance(&game, 500u);
    (void)frame_at(&game, &ui, 2000u);
    CHECK(ui.last_view.sweep > first && ui.last_view.sweep <= jelli_potty_rules.sweep_steps);
    for (unsigned t = 0u; t < 40u && pet->activity != JELLI_IDLE; ++t)
        jelli_game_advance(&game, 500u); /* ...and pushes the mess aside until it is gone. */
    CHECK(!(pet->behavior_flags & JELLI_PET_FLAG_MESS));
    (void)frame_at(&game, &ui, 30000u);
    CHECK(ui.last_view.mess == 0u && !jelli_pet_mess_bounds(&ui, ui.last_view.mess, &mess));
}

int main(void)
{
    behaviour_states_show_their_look();
    mess_shimmers_on_the_floor();
    jelly_profile_matches_legacy_rules();
    every_form_has_grounded_clips();
    axolotl_idles_and_blinks();
    axolotl_sleep_breathes_and_frame_changes_redraw();
    axolotl_actor_sits_on_the_floor();
    puts("PASS: data-driven creature clips animate, redraw, pause, and ground every form");
    return 0;
}
