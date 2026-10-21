#include "jelli/debug.h"
#include "game_fixture.h"
#include "jelli/pet_effect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static JelliPetEngine engine;
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static const uint8_t enabled[JELLI_EFFECT_TRIGGER_COUNT] = {1u, 1u, 1u};
static uint64_t now;
static uint64_t clock_ms(void *ctx)
{
    (void)ctx;
    return now;
}
static void present(void *ctx, const JelliSurface *s)
{
    (void)ctx;
    (void)s;
}
static void event(unsigned kind, unsigned code)
{
    jelli_events_push(&engine.events,
                      (JelliEvent){.kind = (uint8_t)kind,
                                   .code = (uint8_t)code,
                                   .pet_id = engine.game.pets[engine.game.active].id,
                                   .result = JELLI_OK});
}
static void process(uint64_t time, bool suppress, bool paused)
{
    jelli_pet_celebrations(&engine.celebrations, &engine.ui, &engine.game, &engine.events, enabled,
                           time, suppress, paused);
}
static void send(JelliDebug *debug, const char *line, const char *expected)
{
    debug->protocol.reply_size = 0u;
    for (const char *p = line; *p; ++p)
        jelli_debug_feed(debug, &engine, *p, 100u);
    CHECK(debug->protocol.reply_size && strstr(debug->protocol.reply, expected));
}
static void triggers(void)
{
    /* Disabled bindings still consume history, so enabling cannot replay it. */
    static const uint8_t disabled[JELLI_EFFECT_TRIGGER_COUNT] = {0};
    event(JELLI_EVENT_STATUS, 1u);
    jelli_pet_celebrations(&engine.celebrations, &engine.ui, &engine.game, &engine.events, disabled,
                           0u, false, false);
    CHECK(!engine.ui.effect && engine.celebrations.cursor == engine.events.sequence);
    process(0u, false, false);
    CHECK(!engine.ui.effect);
    event(JELLI_EVENT_STATUS, 1u);
    process(10u, false, false);
    CHECK(engine.ui.effect && engine.ui.effect_anchor_ms == 10u);
    jelli_pet_effect_stop(&engine.ui);
    process(20u, false, false);
    CHECK(!engine.ui.effect); /* Exactly once. */
    engine.ui.menu_open = true;
    event(JELLI_EVENT_INPUT, JELLI_INPUT_CATCH_PRESENT);
    process(30u, false, false);
    CHECK(engine.celebrations.pending && !engine.ui.effect);
    engine.ui.menu_open = false;
    process(40u, false, true);
    CHECK(!engine.ui.effect);
    process(50u, false, false);
    CHECK(engine.ui.effect && engine.ui.effect_anchor_ms == 50u);
    event(JELLI_EVENT_STATUS, 1u);
    process(60u, false, false);
    CHECK(engine.celebrations.pending && engine.ui.effect_anchor_ms == 50u);
    jelli_pet_effect_stop(&engine.ui);
    process(70u, false, false);
    CHECK(engine.ui.effect_anchor_ms == 70u);
    jelli_pet_effect_stop(&engine.ui);
}
static void cancellation(void)
{
    event(JELLI_EVENT_STATUS, 1u);
    process(80u, true, false);
    process(90u, false, false);
    CHECK(!engine.ui.effect && !engine.celebrations.pending);
    engine.ui.menu_open = true;
    event(JELLI_EVENT_STATUS, 1u);
    process(100u, false, false);
    ++engine.game.pets[engine.game.active].id;
    process(110u, false, false);
    CHECK(!engine.celebrations.pending);
    event(JELLI_EVENT_STATUS, 1u);
    process(120u, false, false);
    process(30120u, false, false);
    CHECK(!engine.celebrations.pending);
    event(JELLI_EVENT_STATUS, 1u);
    process(30130u, false, false);
    process(0u, false, false);
    CHECK(!engine.celebrations.pending);
    engine.events.sequence = UINT32_MAX;
    engine.celebrations.cursor = UINT32_MAX;
    event(JELLI_EVENT_STATUS, 1u);
    engine.ui.menu_open = false;
    process(10u, false, false);
    CHECK(engine.ui.effect && engine.celebrations.cursor == 1u);
    jelli_pet_effect_stop(&engine.ui);
}
static void live_events(void)
{
    jelli_game_init(&engine.game);
    engine.events = (JelliEventLog){0};
    engine.celebrations = (JelliPetCelebration){0};
    engine.game.events = &engine.events;
    for (unsigned i = 0u; i < 600u; ++i) {
        jelli_game_advance(&engine.game, 100u);
        process((uint64_t)(i + 1u) * 100u, false, false);
    }
    CHECK(engine.game.pets[0].form == 1u && engine.ui.effect);
    jelli_pet_effect_stop(&engine.ui);
    jelli_prize_complete(&engine.game, JELLI_PRIZE_WASH);
    CHECK(engine.game.prizes.offered);
    jelli_pet_ui_tap(&engine.ui, &engine.game, 233, 110);
    CHECK(engine.ui.menu_open && engine.ui.page == JELLI_UI_COLLECTION);
    process(61000u, false, false);
    CHECK(engine.celebrations.pending && !engine.ui.effect);
    engine.ui.menu_open = false;
    process(62000u, false, false);
    CHECK(engine.ui.effect);
    jelli_pet_effect_stop(&engine.ui);
    engine.ui.save_requested = false;
}
static void acceptance(void)
{
    test_game_pair(&engine.game);
    jelli_pet_ui_init(&engine.ui);
    engine.events = (JelliEventLog){0};
    engine.celebrations = (JelliPetCelebration){0};
    engine.game.events = &engine.events;
    jelli_prize_complete(&engine.game, JELLI_PRIZE_WASH);
    CHECK(jelli_prize_catch(&engine.game) == JELLI_OK);
    CHECK(jelli_prize_gift(&engine.game, 5u) == JELLI_NOT_READY);
    process(0u, false, false);
    CHECK(!engine.ui.effect);
    /* A different recipient accepts the collected present immediately. */
    engine.game.prizes.origin_pet[5] = 2u;
    CHECK(jelli_prize_gift(&engine.game, 5u) == JELLI_OK);
    process(10u, false, false);
    CHECK(engine.ui.effect);
    jelli_pet_effect_stop(&engine.ui);
    process(20u, false, false);
    CHECK(!engine.ui.effect);
    /* Legacy inventory gifting celebrates completion, never its start. */
    CHECK(jelli_game_command(&engine.game, (JelliCommand){.kind = JELLI_CMD_GIFT,
                                                          .actor_id = engine.game.pets[0].id}) ==
          JELLI_OK);
    process(30u, false, false);
    CHECK(!engine.ui.effect);
    for (unsigned i = 0u; i < 50u; ++i)
        jelli_game_advance(&engine.game, 100u);
    process(5030u, false, false);
    CHECK(engine.ui.effect);
    jelli_pet_effect_stop(&engine.ui);
    engine.animation_ms = 5030u;
    /* Authored defaults are distinct and the engine uses the generated binding. */
    event(JELLI_EVENT_STATUS, 1u);
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.ui.effect && !strcmp(engine.ui.effect->key, "evolution"));
    jelli_pet_effect_stop(&engine.ui);
}
static void debug_replay(void)
{
    JelliDebug debug = {0};
    engine.ui.save_requested = false;
    engine.animation_ms = 500u;
    send(&debug, "@J1 1 effect play celebrate\n", "\"ok\":true");
    CHECK(engine.ui.effect && engine.ui.effect_anchor_ms == 500u);
    send(&debug, "@J1 2 effect play missing\n", "unknown_effect");
    CHECK(engine.ui.effect);
    send(&debug, "@J1 3 effect\n", "syntax");
    engine.ui.menu_open = true;
    send(&debug, "@J1 4 effect play celebrate\n", "busy");
    engine.ui.menu_open = false;
    debug.captured = true;
    debug.capture_start = debug.capture_activity = 100u;
    send(&debug, "@J1 5 effect play celebrate\n", "busy");
    debug.captured = false;
    send(&debug, "@J1 6 effect stop\n", "\"ok\":true");
    CHECK(!engine.ui.effect && !engine.celebrations.pending);
    CHECK(!engine.ui.save_requested);
}
int main(void)
{
    CHECK(jelli_pet_init(&engine, (JelliPlatform){.now_ms = clock_ms, .present = present},
                         (JelliSurface){pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}}));
    triggers();
    cancellation();
    live_events();
    acceptance();
    debug_replay();
    puts("Celebration event delivery, deferral, cancellation and debug replay passed.");
    return 0;
}
