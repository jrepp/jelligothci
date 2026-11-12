#include "jelli/pet_engine.h"
#include "jelli/gesture.h"
#include "pet_gallery.h"
#include "pet_feedback.h"
#include "pet_collectible_ui.h"
#include <string.h>

_Static_assert(sizeof(JelliPetEngine) <= 32768u, "Pet state exceeds 32 KiB budget");

bool jelli_pet_init(JelliPetEngine *engine, JelliPlatform platform, JelliSurface surface)
{
    if (!engine || !platform.now_ms || !platform.present || !surface.pixels ||
        surface.width != JELLI_WIDTH || surface.height != JELLI_HEIGHT ||
        surface.stride < surface.width)
        return false;
    memset(engine, 0, sizeof(*engine));
    engine->platform = platform;
    engine->surface = surface;
    engine->last_ms = platform.now_ms(platform.ctx);
    engine->running = true;
    engine->motion_result = JELLI_DEVICE_UNAVAILABLE;
    jelli_game_init(&engine->game);
    engine->game.events = &engine->events;
    jelli_pet_ui_init(&engine->ui);
    return true;
}

void jelli_pet_bind_devices(JelliPetEngine *engine, JelliMotionDriver motion,
                            JelliDisplayDriver display)
{
    engine->motion_driver = motion;
    engine->display_driver = display;
    engine->motion = (JelliMotionSample){0};
    engine->motion_result = JELLI_DEVICE_UNAVAILABLE;
}

static void sample_motion(JelliPetEngine *engine, uint64_t now)
{
    JelliMotionSample sample = {0};
    JelliMotionDriver driver = engine->motion_driver;
    engine->motion_result =
        driver.poll ? driver.poll(driver.ctx, &sample) : JELLI_DEVICE_UNAVAILABLE;
    if (engine->motion_result == JELLI_DEVICE_OK && sample.detected && sample.observed_ms > now)
        engine->motion_result = JELLI_DEVICE_RESPONSE;
    engine->motion = engine->motion_result == JELLI_DEVICE_OK ? sample : (JelliMotionSample){0};
}

static void advance(JelliPetEngine *engine, uint64_t elapsed)
{
    const JelliPet *pet = &engine->game.pets[engine->game.active];
    uint32_t scale =
        jelli_tunable_get(&engine->ui.tunables, pet->id, pet->form, JELLI_TUNE_ANIMATION_SCALE);
    jelli_particles_advance_scaled(&engine->ui.particles, elapsed, 20u * scale / 100u);
    if (!engine->paused)
        engine->animation_ms += elapsed; /* Unsigned wrap is handled by renderer re-anchoring. */
    if (engine->game.resuming)
        (void)jelli_game_resume_step(&engine->game);
    else if (!engine->paused)
        jelli_game_advance(&engine->game, elapsed);
}

static bool consume_input(JelliPetEngine *engine, JelliInput input)
{
    if (engine->hold_active &&
        (input.kind == JELLI_PRESS_CANCEL ||
         ((input.kind == JELLI_PRESS_MOVE || input.kind == JELLI_LONG_PRESS) &&
          !jelli_pet_ui_menu_hit(&engine->ui, &engine->game, input.x, input.y))))
        engine->hold_consumed = true;
    if (input.kind == JELLI_PRESS_BEGIN)
        engine->hold_consumed = false;
    else if (input.kind != JELLI_PRESS_MOVE) {
        engine->hold_active = false;
        if (engine->hold_consumed && (input.kind == JELLI_TAP || input.kind == JELLI_SWIPE ||
                                      input.kind == JELLI_LONG_PRESS)) {
            engine->hold_consumed = false;
            return true;
        }
    }
    return false;
}

static void input_event(JelliPetEngine *engine, JelliInput input)
{
    if (consume_input(engine, input))
        return;
    switch (input.kind) {
    case JELLI_QUIT:
        engine->running = false;
        break;
    case JELLI_TAP:
        if (!engine->game.resuming)
            jelli_pet_ui_tap(&engine->ui, &engine->game, input.x, input.y);
        break;
    case JELLI_PRESS_BEGIN:
        engine->hold_active = jelli_pet_ui_menu_hit(&engine->ui, &engine->game, input.x, input.y);
        engine->hold_started_ms = engine->last_ms;
        engine->hold_x = input.x;
        engine->hold_y = input.y;
        break;
    case JELLI_PRESS_MOVE:
        if (engine->hold_active &&
            !jelli_pet_ui_menu_hit(&engine->ui, &engine->game, input.x, input.y))
            engine->hold_active = false;
        break;
    case JELLI_PRESS_CANCEL:
        break;
    case JELLI_LONG_PRESS:
        if (jelli_pet_ui_menu_hit(&engine->ui, &engine->game, input.x, input.y))
            jelli_pet_ui_long_press(&engine->ui, &engine->game, input.x, input.y);
        else
            jelli_pet_ui_tap(&engine->ui, &engine->game, input.x, input.y);
        break;
    case JELLI_SWIPE:
        jelli_pet_ui_swipe(&engine->ui, &engine->game, input.x, input.y);
        break;
    case JELLI_TOGGLE_PAUSE:
        engine->paused = !engine->paused;
        if (engine->platform.paused)
            engine->platform.paused(engine->platform.ctx, engine->paused);
        break;
    }
}

static void rewards(JelliPetEngine *engine)
{
    JelliPetUi *ui = &engine->ui;
    uint32_t completed = ui->rewards.completed;
    bool routine =
        ui->menu_open && jelli_pet_page_is_routine(ui->page) && ui->page < JELLI_UI_PAGE_COUNT;
    if (jelli_pet_rewards_process(&ui->rewards, &engine->game, routine, ui->clicker_done,
                                  ui->clicker_pet)) {
        ui->menu_open = false;
        ui->page = JELLI_UI_HOME;
    }
    if (ui->rewards.completed != completed) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
}

static void update_hold(JelliPetEngine *engine, uint64_t now)
{
    if (engine->hold_active &&
        (now < engine->hold_started_ms || engine->ui.menu_open || engine->game.resuming)) {
        engine->hold_active = false;
        engine->hold_consumed = true;
    }
    uint64_t held = engine->hold_active ? now - engine->hold_started_ms : 0u;
    if (engine->hold_active && held >= JELLI_HOLD_MS) {
        engine->hold_active = false;
        engine->hold_consumed = true;
        jelli_pet_ui_long_press(&engine->ui, &engine->game, engine->hold_x, engine->hold_y);
        held = 0u;
    }
    uint8_t progress = (uint8_t)(held / (JELLI_HOLD_MS / 12u));
    if (progress != engine->ui.hold_progress) {
        engine->ui.hold_progress = progress;
        engine->ui.rendered = false;
    }
}

bool jelli_pet_frame(JelliPetEngine *engine)
{
    if (!engine || !engine->running)
        return false;
    engine->game.events = &engine->events; /* Reattach after loading a snapshot. */
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0;
    engine->last_ms = now;
    sample_motion(engine, now);
    jelli_pet_music_progress(engine);
    bool was_resuming = engine->game.resuming;
    JelliCollectibleSession previous_collectible = engine->game.collectible;
    rewards(engine); /* Includes commands submitted through the external debug interface. */
    advance(engine, elapsed);
    if (!was_resuming)
        rewards(engine);
    else {
        jelli_pet_rewards_cancel(&engine->ui.rewards);
        engine->ui.rewards.cursor = engine->events.sequence;
    }
    JelliInput input;
    for (unsigned n = 0;
         engine->platform.poll && n < 32 && engine->platform.poll(engine->platform.ctx, &input);
         ++n) {
        /* The host commits a resumed snapshot before admitting new actions. */
        if (!was_resuming || input.kind == JELLI_QUIT) {
            input_event(engine, input);
            rewards(engine);
        }
    }
    if (!engine->running) {
        engine->paused = true;
        jelli_pet_music_sync(engine);
        return false;
    }
    if (!was_resuming && !engine->paused)
        jelli_pet_chain_feedback(&engine->ui, &previous_collectible, &engine->game.collectible);
    jelli_pet_bubble_checkpoint(&engine->ui, &previous_collectible, &engine->game.collectible);
    jelli_pet_music_sync(engine);
    jelli_pet_celebrations(&engine->celebrations, &engine->ui, &engine->game, &engine->events,
                           jelli_effect_triggers, engine->animation_ms,
                           was_resuming || engine->game.resuming, engine->paused);
    jelli_pet_gallery_update(&engine->ui, &engine->game, engine->animation_ms);
    update_hold(engine, now);
    jelli_pet_render(&engine->surface, &engine->game, &engine->ui, engine->animation_ms,
                     engine->paused);
    engine->platform.present(engine->platform.ctx, &engine->surface);
    return true;
}
