#include "jelli/pet_engine.h"
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
    jelli_pet_ui_init(&engine->ui);
    return true;
}

static void advance(JelliPetEngine *engine, uint64_t elapsed)
{
    engine->animation_ms = (engine->animation_ms + (uint32_t)(elapsed % 900u)) % 900u;
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
    case JELLI_TOGGLE_PAUSE:
        engine->paused = !engine->paused;
        if (engine->platform.paused)
            engine->platform.paused(engine->platform.ctx, engine->paused);
        break;
    }
}

bool jelli_pet_frame(JelliPetEngine *engine)
{
    if (!engine || !engine->running)
        return false;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0;
    engine->last_ms = now;
    bool was_resuming = engine->game.resuming;
    advance(engine, elapsed);
    JelliInput input;
    for (unsigned n = 0;
         engine->platform.poll && n < 32 && engine->platform.poll(engine->platform.ctx, &input);
         ++n) {
        /* The host commits a resumed snapshot before admitting new actions. */
        if (!was_resuming || input.kind == JELLI_QUIT)
            input_event(engine, input);
    }
    if (!engine->running)
        return false;
    jelli_pet_render(&engine->surface, &engine->game, &engine->ui, engine->animation_ms,
                     engine->paused);
    engine->platform.present(engine->platform.ctx, &engine->surface);
    return true;
}
