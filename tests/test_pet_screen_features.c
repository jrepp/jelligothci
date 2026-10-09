/* Screen sleep against the features that keep running behind a dark panel: the pet's
 * own sleep and the sleep log, offline catch-up, saves requested while dark, the bobbing
 * present's partial redraw, battery readings, and pause. A fake host counts presents
 * and pause callbacks on injected time; a second engine kept awake is the reference. */
#include "jelli/debug.h"
#include "jelli/save.h"
#include "../core/pet_gallery.h" /* Internal catch-rect bounds under test. */
#include "../core/pet_battery.h" /* Internal battery bounds under test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { DARK = 0, LIT = 1, PIXELS = JELLI_WIDTH * JELLI_HEIGHT };

typedef struct {
    uint64_t now;
    unsigned presents, pauses;
    JelliRect damage;
    JelliInput queue[8];
    unsigned head, count;
} Host;

static Host hosts[2];
static JelliPetEngine engines[2], scratch;
static JelliDebug debug;
static uint16_t pixels[2][PIXELS], fresh[PIXELS], before[PIXELS];
static uint8_t encoded[2][JELLI_SAVE_CAPACITY];

static uint64_t now_ms(void *context) { return ((Host *)context)->now; }

static bool poll(void *context, JelliInput *input)
{
    Host *host = context;
    if (!host->count)
        return false;
    *input = host->queue[host->head];
    host->head = (host->head + 1u) % 8u;
    --host->count;
    return true;
}

static void present(void *context, const JelliSurface *surface)
{
    Host *host = context;
    CHECK(host != &hosts[DARK] || jelli_pet_screen_awake(&engines[DARK]));
    ++host->presents;
    host->damage = surface->damage;
}

static void paused(void *context, bool value)
{
    (void)value;
    ++((Host *)context)->pauses;
}

static JelliPetEngine *start(unsigned slot, uint64_t at)
{
    hosts[slot] = (Host){.now = at};
    JelliPlatform platform = {&hosts[slot], now_ms, poll, present, paused};
    JelliSurface surface = {pixels[slot], JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    CHECK(jelli_pet_init(&engines[slot], platform, surface));
    CHECK(jelli_pet_frame(&engines[slot]));
    return &engines[slot];
}

static void push(unsigned slot, JelliInputKind kind, int x, int y)
{
    CHECK(hosts[slot].count < 8u);
    hosts[slot].queue[(hosts[slot].head + hosts[slot].count++) % 8u] = (JelliInput){kind, x, y};
}

/* Runs a frame on each started engine at `at`; the lit reference never sleeps. */
static void frames_at(uint64_t at, bool both)
{
    hosts[DARK].now = hosts[LIT].now = at;
    CHECK(jelli_pet_frame(&engines[DARK]));
    if (both) {
        jelli_pet_screen_wake(&engines[LIT], at);
        CHECK(jelli_pet_frame(&engines[LIT]));
    }
}

static bool full(JelliRect r)
{
    return r.x == 0u && r.y == 0u && r.width == JELLI_WIDTH && r.height == JELLI_HEIGHT;
}

static bool same_rect(JelliRect a, JelliRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

/* The dark engine's panel equals a fresh full render of its current state. */
static bool matches_fresh_render(void)
{
    scratch = engines[DARK];
    scratch.ui.rendered = false;
    scratch.surface.pixels = fresh;
    jelli_pet_render(&scratch.surface, &scratch.game, &scratch.ui, scratch.animation_ms,
                     scratch.paused);
    return memcmp(fresh, pixels[DARK], sizeof(fresh)) == 0;
}

static void double_tap(uint64_t at)
{
    push(DARK, JELLI_TAP, 233, 233);
    frames_at(at, false);
    push(DARK, JELLI_TAP, 240, 228);
    frames_at(at + 200u, false);
    CHECK(jelli_pet_screen_awake(&engines[DARK]));
}

static void same_games(void)
{
    JelliSave save = {.game = engines[DARK].game, .sequence = 1u};
    size_t size = jelli_save_encode(&save, encoded[0], JELLI_SAVE_CAPACITY);
    save.game = engines[LIT].game;
    CHECK(size > 0u && size == jelli_save_encode(&save, encoded[1], JELLI_SAVE_CAPACITY));
    CHECK(memcmp(encoded[0], encoded[1], size) == 0);
    CHECK(engines[DARK].events.sequence == engines[LIT].events.sequence);
}

/* The pet rests, six and a half hours pass offline while the panel is dark, and it wakes:
 * the sleep log, the six-hour sleep prize, and wake rewards match a lit engine, with no
 * present or cue from the dark one. The reward reveal waits for the screen. */
static void pet_sleeps_through_a_dark_screen(void)
{
    JelliPetEngine *dark = start(DARK, 1000u);
    const JelliPetEngine *lit = start(LIT, 1000u);
    for (unsigned slot = 0u; slot < 2u; ++slot) {
        JelliGame *game = &engines[slot].game;
        CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_REST, game->pets[game->active].id,
                                                      0u}) == JELLI_OK);
    }
    jelli_pet_screen_sleep(dark, 1000u);
    unsigned presents = hosts[DARK].presents, cues = 0u;
    for (unsigned slot = 0u; slot < 2u; ++slot)
        jelli_game_resume_begin(&engines[slot].game, UINT64_C(23400000));
    uint64_t now = 1016u;
    for (unsigned n = 0u; n < 2000u && (dark->game.resuming || lit->game.resuming);
         ++n, now += 16u) {
        frames_at(now, true);
        cues += jelli_pet_sound_cue(dark, now) != 0u;
    }
    CHECK(!dark->game.resuming && !lit->game.resuming && dark->game.pets[dark->game.active].asleep);
    same_games();
    uint32_t completed = dark->ui.rewards.completed;
    for (unsigned slot = 0u; slot < 2u; ++slot) {
        JelliGame *game = &engines[slot].game;
        CHECK(jelli_game_command(game, (JelliCommand){JELLI_CMD_WAKE, game->pets[game->active].id,
                                                      0u}) == JELLI_OK);
    }
    for (unsigned n = 0u; n < 200u; ++n, now += 16u) {
        frames_at(now, true);
        cues += jelli_pet_sound_cue(dark, now) != 0u;
    }
    same_games();
    const JelliSleepLog *log = &dark->game.sleep_log;
    const JelliSleepSession *session =
        &log->sessions[(log->head + JELLI_SLEEP_SESSION_CAPACITY - 1u) %
                       JELLI_SLEEP_SESSION_CAPACITY];
    CHECK(!log->active && session->duration_seconds >= 21600u);        /* Both logs: same_games. */
    CHECK(dark->ui.rewards.completed == completed + 1u);               /* Rewarded while dark... */
    CHECK(dark->ui.rewards.count > 0u && lit->ui.rewards.count == 0u); /* ...shown later. */
    CHECK(hosts[DARK].presents == presents && cues == 0u && !jelli_pet_screen_awake(dark));
    double_tap(now);
    CHECK(full(hosts[DARK].damage) && matches_fresh_render());
    for (unsigned n = 0u; n < 400u && dark->ui.rewards.count; ++n)
        frames_at(now += 16u, true);
    CHECK(dark->ui.rewards.count == 0u); /* The reveal played once the panel was lit. */
}

/* Debug cheats and reward completions mark a save while dark without waking the panel. */
static void saves_requested_while_dark(void)
{
    JelliPetEngine *dark = start(DARK, 0u);
    jelli_pet_screen_sleep(dark, 0u);
    dark->ui.save_requested = false;
    memset(&debug, 0, sizeof(debug));
    static const char line[] = "@J1 1 cheat food 5\n";
    for (size_t i = 0u; line[i]; ++i)
        jelli_debug_feed(&debug, dark, line[i], 10u);
    CHECK(strstr(debug.reply, "\"cheat\":\"applied\"") != NULL);
    CHECK(dark->ui.save_requested && dark->game.food == 5u);
    frames_at(20u, false);
    CHECK(!jelli_pet_screen_awake(dark) && dark->ui.save_requested);
    JelliSave save = {.game = dark->game, .sequence = 1u};
    CHECK(jelli_save_encode(&save, encoded[0], JELLI_SAVE_CAPACITY) > 0u); /* Saveable. */
}

/* The bob stops presenting while dark; after the wake's full redraw, bob steps go back to
 * repainting only the present, each still equal to a full redraw. */
static void bobbing_present_while_dark(void)
{
    JelliPetEngine *dark = start(DARK, 0u);
    jelli_prize_complete(&dark->game, JELLI_PRIZE_WASH);
    CHECK(dark->game.prizes.offered != 0u && jelli_game_valid(&dark->game));
    const JelliRect bob = jelli_pet_gallery_catch_bounds();
    unsigned partial = 0u;
    uint64_t now = 0u;
    for (unsigned n = 0u; n < 400u; ++n) {
        frames_at(now += 16u, false);
        partial += same_rect(hosts[DARK].damage, bob);
    }
    CHECK(partial > 5u);
    jelli_pet_screen_sleep(dark, now);
    unsigned presents = hosts[DARK].presents;
    for (unsigned n = 0u; n < 2000u; ++n)
        frames_at(now += 16u, false);
    CHECK(hosts[DARK].presents == presents);
    double_tap(now += 16u);
    now += 200u;
    CHECK(full(hosts[DARK].damage) && matches_fresh_render());
    partial = 0u;
    for (unsigned n = 0u; n < 200u; ++n) {
        frames_at(now += 16u, false);
        bool step = same_rect(hosts[DARK].damage, bob);
        partial += step;
        CHECK(!step || matches_fresh_render());
    }
    CHECK(partial > 3u);
}

/* Host battery readings and debug overrides while dark never present or wake; the wake
 * shows the latest reading. */
static void battery_while_dark(void)
{
    JelliPetEngine *dark = start(DARK, 0u);
    dark->game.battery_display = true;
    dark->ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 80u};
    frames_at(16u, false);
    frames_at(32u, false);
    memcpy(before, pixels[DARK], sizeof(before));
    jelli_pet_screen_sleep(dark, 32u);
    unsigned presents = hosts[DARK].presents;
    static const uint8_t readings[] = {60u, 15u, 3u};
    uint64_t now = 32u;
    for (unsigned i = 0u; i < 3u; ++i) {
        dark->ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, readings[i]};
        frames_at(now += 16u, false);
    }
    memset(&debug, 0, sizeof(debug));
    static const char line[] = "@J1 2 battery charging 42\n";
    for (size_t i = 0u; line[i]; ++i)
        jelli_debug_feed(&debug, dark, line[i], now);
    CHECK(strstr(debug.reply, "\"ok\":true") != NULL);
    frames_at(now + 16u, false);
    CHECK(hosts[DARK].presents == presents && !jelli_pet_screen_awake(dark));
    double_tap(now + 32u);
    CHECK(full(hosts[DARK].damage) && matches_fresh_render());
    const JelliRect b = jelli_pet_battery_bounds();
    bool changed = false;
    for (unsigned y = b.y; y < b.y + b.height; ++y)
        changed |= memcmp(&before[y * JELLI_WIDTH + b.x], &pixels[DARK][y * JELLI_WIDTH + b.x],
                          b.width * sizeof(uint16_t)) != 0;
    CHECK(changed); /* The latest reading, not the pre-sleep one. */
}

/* A paused engine still sleeps; toggles while dark are ignored and nothing is consumed;
 * the wake keeps the pause; QUIT still exits while paused and dark. */
static void pause_while_dark(void)
{
    JelliPetEngine *dark = start(DARK, 0u);
    push(DARK, JELLI_TOGGLE_PAUSE, 0, 0);
    frames_at(1000u, false);
    CHECK(dark->paused && hosts[DARK].pauses == 1u);
    frames_at(180999u, false);
    CHECK(jelli_pet_screen_awake(dark));
    frames_at(181000u, false);
    CHECK(!jelli_pet_screen_awake(dark));
    uint64_t animation = dark->animation_ms;
    push(DARK, JELLI_TOGGLE_PAUSE, 0, 0);
    push(DARK, JELLI_TOGGLE_PAUSE, 0, 0);
    push(DARK, JELLI_TOGGLE_PAUSE, 0, 0);
    frames_at(190000u, false);
    CHECK(dark->paused && hosts[DARK].pauses == 1u && dark->animation_ms == animation);
    dark->ui.sound_pending = true; /* Paused: neither played nor consumed, dark or not. */
    CHECK(jelli_pet_sound_cue(dark, 190000u) == 0u && dark->ui.sound_pending);
    double_tap(191000u);
    CHECK(dark->paused && hosts[DARK].pauses == 1u && full(hosts[DARK].damage));
    push(DARK, JELLI_TOGGLE_PAUSE, 0, 0); /* Awake again: the toggle works. */
    frames_at(192000u, false);
    CHECK(!dark->paused && hosts[DARK].pauses == 2u);
    push(DARK, JELLI_TOGGLE_PAUSE, 0, 0);
    frames_at(193000u, false);
    jelli_pet_screen_sleep(dark, 193000u);
    push(DARK, JELLI_TAP, 233, 233);
    push(DARK, JELLI_QUIT, 0, 0);
    hosts[DARK].now = 194000u;
    CHECK(!jelli_pet_frame(dark) && !dark->running && dark->paused);
}

/* Offline catch-up while dark: a double tap still wakes (the gate runs before the resume
 * check) and the waking frame draws the resume state, yet no tap reaches the UI until the
 * snapshot is committed. */
static void resume_while_dark(void)
{
    JelliPetEngine *dark = start(DARK, 0u);
    jelli_pet_screen_sleep(dark, 0u);
    jelli_game_resume_begin(&dark->game, UINT64_C(3600000));
    CHECK(dark->game.resuming);
    push(DARK, JELLI_TAP, 233, 233);
    push(DARK, JELLI_TAP, 233, 233);
    frames_at(16u, false);
    CHECK(jelli_pet_screen_awake(dark) && dark->game.resuming && full(hosts[DARK].damage));
    CHECK(matches_fresh_render());
    push(DARK, JELLI_TAP, 233, 233); /* Past the guard but still resuming: not delivered. */
    frames_at(1000u, false);
    CHECK(dark->game.resuming && dark->game.pets[dark->game.active].reaction == 0u);
    uint64_t now = 1016u;
    for (; dark->game.resuming && now < 200000u; now += 16u)
        frames_at(now, false);
    CHECK(!dark->game.resuming && jelli_pet_screen_awake(dark));
    uint32_t events = dark->events.sequence;
    push(DARK, JELLI_TAP, 233, 233); /* Control: the same tap touches the pet once committed. */
    frames_at(now, false);
    CHECK(dark->game.pets[dark->game.active].reaction != 0u && dark->events.sequence > events);
}

/* The change query also consumes an edge when the host passes no output. */
static void change_query_without_output(void)
{
    JelliPetEngine *dark = start(DARK, 0u);
    jelli_pet_screen_sleep(dark, 0u);
    CHECK(jelli_pet_screen_changed(dark, NULL));
    bool awake = true;
    CHECK(!jelli_pet_screen_changed(dark, &awake) && awake); /* Unchanged output on false. */
    jelli_pet_screen_wake(dark, 0u);
    CHECK(jelli_pet_screen_changed(dark, &awake) && awake);
    jelli_pet_screen_sleep(NULL, 0u);
    jelli_pet_screen_wake(NULL, 0u);
}

int main(void)
{
    pet_sleeps_through_a_dark_screen();
    saves_requested_while_dark();
    bobbing_present_while_dark();
    battery_while_dark();
    pause_while_dark();
    resume_while_dark();
    change_query_without_output();
    printf("Screen sleep feature interaction checks passed\n");
    return 0;
}
