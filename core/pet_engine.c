#include "jelli/pet_engine.h"
#include "pet_gallery.h"
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
    jelli_game_init(&engine->game);
    engine->game.events = &engine->events;
    jelli_pet_ui_init(&engine->ui);
    return true;
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

static void input_event(JelliPetEngine *engine, JelliInput input)
{
    switch (input.kind) {
    case JELLI_QUIT:
        engine->running = false;
        break;
    case JELLI_TAP:
        if (!engine->game.resuming)
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
    bool routine = ui->menu_open && (ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH) &&
                   ui->page < JELLI_UI_PAGE_COUNT;
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

bool jelli_pet_frame(JelliPetEngine *engine)
{
    if (!engine || !engine->running)
        return false;
    engine->game.events = &engine->events; /* Reattach after loading a snapshot. */
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0;
    engine->last_ms = now;
    bool was_resuming = engine->game.resuming;
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
    if (!engine->running)
        return false;
    jelli_pet_gallery_update(&engine->ui, &engine->game, engine->animation_ms);
    jelli_pet_render(&engine->surface, &engine->game, &engine->ui, engine->animation_ms,
                     engine->paused);
    engine->platform.present(engine->platform.ctx, &engine->surface);
    return true;
}
