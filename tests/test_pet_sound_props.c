/* Seeded property tests for the Settings sound editor. Random taps (every ring slot,
 * the clock gear, MENU/BACK, the pet, random and off-panel points), swipes, BACK,
 * sleep/wake, pet switches, and time steps (paused or not) must never open both
 * editors, never strand an editor outside Settings, keep the level within 0..10,
 * request a save exactly on an accepted level change, emit the documented events
 * (42 open, 43 VOL -, 44 VOL +, NOT_READY unchanged at a limit), and keep every
 * partial redraw equal to a full one. A failure prints the seed and step to replay. */
#include "jelli/pet_engine.h"
#include "../core/pet_canvas.h" /* Internal round-panel mask under test. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define CHECK_STEP(expression, world)                                                              \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x step %u action %u)\n", __FILE__, __LINE__,     \
                    #expression, (unsigned)(world)->seed, (world)->step, (world)->action);         \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT, STEPS = 500u, SENTINEL = 0xa55a };
enum { OPEN = JELLI_UI_INPUT_SOUND, MINUS = JELLI_UI_INPUT_SOUND + 1u, PLUS = OPEN + 2u };
static uint16_t pixels[PIXELS], fresh[PIXELS], outside[PIXELS];

/* Settings ring centers (slots 1..6), the clock gear, MENU, BACK, the pet, the panel
 * edge, and both off-panel corners. VOL - and VOL + share slots 6 and 3's centers. */
static const int targets[][2] = {{110, 111}, {356, 111}, {405, 233}, {356, 355}, {110, 355},
                                 {61, 233},  {233, 267}, {233, 420}, {233, 440}, {233, 233},
                                 {233, 160}, {0, 0},     {465, 465}};
enum { TARGETS = sizeof(targets) / sizeof(targets[0]) };

typedef struct {
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    uint64_t time;
    uint32_t seed, state;
    unsigned step, action, opened, plus_ok, minus_ok, low_refused, high_refused, clock_opened;
    unsigned sound_frames, full_checks;
    bool paused, up;
} World;

typedef struct {
    uint8_t level;
    bool sound_edit, clock_edit, settings;
    uint32_t sequence;
} Before;

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void render(World *w, bool check_full)
{
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &w->game, &w->ui, w->time, w->paused);
    for (size_t y = 0u; y < JELLI_HEIGHT; ++y) /* Stride padding is never written. */
        for (size_t x = JELLI_WIDTH; x < STRIDE; ++x)
            CHECK_STEP(pixels[y * STRIDE + x] == SENTINEL, w);
    /* A fresh renderer snaps ring and stat-tile transitions to their targets and ignores
     * a paused frame's frozen phase, so compare settled, running Settings frames. */
    if (!check_full || w->paused || w->ui.last_view.ring_moving || !w->ui.menu_open ||
        w->ui.page != JELLI_UI_SETTINGS)
        return;
    w->full_checks += 1u;
    JelliPetUi copy = w->ui;
    copy.rendered = false;
    JelliSurface full = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&full, &w->game, &copy, w->time, false);
    for (size_t y = 0u; y < JELLI_HEIGHT; ++y)
        CHECK_STEP(
            memcmp(&pixels[y * STRIDE], &fresh[y * STRIDE], sizeof(fresh[0]) * JELLI_WIDTH) == 0,
            w);
    for (int y = 0; y < (int)JELLI_HEIGHT; ++y) /* Off the round panel: as first drawn. */
        for (int x = 0; x < (int)JELLI_WIDTH; ++x)
            if (!jelli_canvas_in_round(x, y))
                CHECK_STEP(pixels[(size_t)y * STRIDE + (size_t)x] ==
                               outside[(size_t)y * STRIDE + (size_t)x],
                           w);
}

static void command(World *w, JelliCommandKind kind, uint32_t value)
{
    const JelliPet *pet = &w->game.pets[w->game.active];
    (void)jelli_game_command(&w->game, (JelliCommand){kind, pet->id, value});
}

/* One random input; the weights favour ring taps so every editor state is reached. */
static void act(World *w)
{
    uint32_t pick = next_random(&w->state) % 32u;
    w->action = pick;
    if (w->ui.sound_edit && pick < 22u) { /* Runs of presses reach both limits. */
        if (w->game.sound_level == (w->up ? JELLI_SOUND_LEVEL_MAX : 0u) && pick < 4u)
            w->up = !w->up;
        jelli_pet_ui_tap(&w->ui, &w->game, w->up ? 405 : 61, 233);
    } else if (pick < 20u) {
        const int *t = targets[next_random(&w->state) % TARGETS];
        jelli_pet_ui_tap(&w->ui, &w->game, t[0], t[1]);
    } else if (pick < 22u) {
        jelli_pet_ui_tap(&w->ui, &w->game, (int)(next_random(&w->state) % 482u) - 8,
                         (int)(next_random(&w->state) % 482u) - 8);
    } else if (pick < 25u) {
        static const int swipes[4][2] = {{0, 80}, {0, -80}, {80, 0}, {-80, 0}};
        const int *s = swipes[next_random(&w->state) % 4u];
        jelli_pet_ui_swipe(&w->ui, &w->game, s[0], s[1]);
    } else if (pick < 26u) {
        jelli_pet_ui_back(&w->ui);
    } else if (pick < 27u) {
        command(w, w->game.pets[w->game.active].asleep ? JELLI_CMD_WAKE : JELLI_CMD_REST, 0u);
    } else if (pick < 28u) {
        command(w, JELLI_CMD_ACTIVATE, w->game.pets[next_random(&w->state) % w->game.count].id);
    } else if (pick < 30u && !w->ui.clock_edit && !w->ui.sound_edit) {
        w->ui.menu_open = true; /* Shortcut to Settings from a clean state. */
        w->ui.page = JELLI_UI_SETTINGS;
    } else {
        w->paused = next_random(&w->state) % 8u == 0u;
    }
}

static void check_sound_event(World *w, const Before *b, const JelliEvent *e)
{
    int delta = (int)w->game.sound_level - (int)b->level;
    CHECK_STEP(e->input == e->code + 1u, w);
    if (e->code == OPEN) {
        CHECK_STEP(b->settings && !b->sound_edit && !b->clock_edit && e->result == JELLI_OK, w);
        CHECK_STEP(w->ui.sound_edit && delta == 0 && !w->ui.save_requested, w);
        ++w->opened;
        return;
    }
    CHECK_STEP(b->sound_edit && !b->clock_edit, w); /* VOL buttons exist only while editing. */
    bool minus = e->code == MINUS;
    if (e->result == JELLI_OK) {
        CHECK_STEP(delta == (minus ? -1 : 1) && w->ui.save_requested, w);
        CHECK_STEP(w->ui.save_status == JELLI_SAVE_PENDING, w);
        w->minus_ok += minus ? 1u : 0u;
        w->plus_ok += minus ? 0u : 1u;
        return;
    }
    /* Refused only at the limit, and then nothing changes or persists. */
    CHECK_STEP(e->result == JELLI_NOT_READY && delta == 0 && !w->ui.save_requested, w);
    CHECK_STEP(b->level == (minus ? 0u : JELLI_SOUND_LEVEL_MAX), w);
    w->low_refused += minus ? 1u : 0u;
    w->high_refused += minus ? 0u : 1u;
}

/* Every sound input event of this step agrees with the level and save request. */
static void check_events(World *w, const Before *b)
{
    unsigned sound = 0u;
    for (unsigned i = 0u; i < w->events.count; ++i) {
        const JelliEvent *e = jelli_events_at(&w->events, i);
        if (!e || e->sequence <= b->sequence)
            continue;
        if (e->kind == JELLI_EVENT_INPUT && e->code >= OPEN && e->code <= PLUS) {
            ++sound;
            check_sound_event(w, b, e);
        } else if (e->kind == JELLI_EVENT_INPUT && e->code == 33u) { /* CLOCK opened. */
            CHECK_STEP(!b->sound_edit, w);
            w->clock_opened += 1u;
        }
    }
    CHECK_STEP(sound <= 1u, w);
    if (w->game.sound_level != b->level) /* No level change without its event. */
        CHECK_STEP(sound == 1u, w);
}

static void check_state(World *w)
{
    const JelliPetUi *ui = &w->ui;
    CHECK_STEP(!(ui->clock_edit && ui->sound_edit), w);
    if (ui->sound_edit || ui->clock_edit) /* An editor is never stranded off Settings. */
        CHECK_STEP(ui->menu_open && ui->page == JELLI_UI_SETTINGS, w);
    CHECK_STEP(w->game.sound_level <= JELLI_SOUND_LEVEL_MAX && jelli_game_valid(&w->game), w);
    CHECK_STEP(w->game.sound_level ==
                   JELLI_SOUND_LEVEL_DEFAULT + w->plus_ok - w->minus_ok, /* Conservation. */
               w);
    CHECK_STEP(ui->last_view.sound_level == w->game.sound_level, w);
    CHECK_STEP(ui->last_view.sound_edit == ui->sound_edit, w);
    if (!ui->sound_edit)
        return;
    w->sound_frames += 1u;
    JelliResult low = jelli_pet_ui_available(&w->ui, &w->game, 1u);
    JelliResult high = jelli_pet_ui_available(&w->ui, &w->game, 2u);
    CHECK_STEP((low == JELLI_NOT_READY) == (w->game.sound_level == 0u), w);
    CHECK_STEP((high == JELLI_NOT_READY) == (w->game.sound_level == JELLI_SOUND_LEVEL_MAX), w);
    JelliPetUiButton button;
    for (unsigned slot = 3u; slot <= 6u; ++slot)
        CHECK_STEP(!jelli_pet_ui_control(&w->ui, slot, false, &button), w);
}

static void run(uint32_t seed, World *w)
{
    memset(w, 0, sizeof(*w));
    w->seed = w->state = seed;
    w->up = (seed & 2u) != 0u; /* Half the walks press up first, half down. */
    jelli_game_init(&w->game);
    jelli_pet_ui_init(&w->ui);
    w->game.events = &w->events;
    for (size_t i = 0u; i < PIXELS; ++i)
        pixels[i] = fresh[i] = SENTINEL;
    render(w, false);
    memcpy(outside, pixels, sizeof(outside));
    for (w->step = 0u; w->step < STEPS; ++w->step) {
        Before b = {w->game.sound_level, w->ui.sound_edit, w->ui.clock_edit,
                    w->ui.menu_open && w->ui.page == JELLI_UI_SETTINGS, w->events.sequence};
        w->ui.save_requested = false; /* The host saved the previous request. */
        act(w);
        check_events(w, &b);
        w->time += next_random(&w->state) % 400u;
        render(w, w->step % 3u == 0u);
        check_state(w);
    }
}

typedef struct {
    uint64_t now;
    JelliInput pending;
    bool ready;
} Host;

static uint64_t host_now(void *context) { return ((Host *)context)->now; }

static bool host_poll(void *context, JelliInput *input)
{
    Host *host = context;
    if (!host->ready)
        return false;
    *input = host->pending;
    host->ready = false;
    return true;
}

static void host_present(void *context, const JelliSurface *surface)
{
    (void)context;
    CHECK(surface->pixels != NULL);
}

/* One random input, biased toward VOL presses while the editor is open. */
static JelliInput audio_input(const JelliPetEngine *engine, uint32_t *state)
{
    uint32_t pick = next_random(state) % 16u;
    if (engine->ui.sound_edit && pick < 8u) /* Mostly up from mute: limits stay reachable. */
        return (JelliInput){JELLI_TAP, pick < 5u ? 405 : 61, 233};
    if (pick < 14u) {
        const int *t = targets[next_random(state) % TARGETS];
        return (JelliInput){JELLI_TAP, t[0], t[1]};
    }
    return (JelliInput){JELLI_SWIPE, 0, pick == 14u ? 80 : -80};
}

/* Like both ports: the engine frame, then the cue at the level that frame left. A
 * muted frame never requests anything, whatever the cue; a VOL press that changed
 * the level is heard in the same frame as the tap cue at the new level. */
static void host_audio_walk(uint32_t seed)
{
    static uint16_t surface_pixels[JELLI_WIDTH * JELLI_HEIGHT];
    static JelliPetEngine engine;
    Host host = {0};
    JelliPlatform platform = {&host, host_now, host_poll, host_present, NULL};
    JelliSurface surface = {surface_pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    CHECK(jelli_pet_init(&engine, platform, surface));
    engine.game.sound_level = 0u;
    uint32_t state = seed;
    unsigned muted_cues = 0u, requests = 0u, heard_changes = 0u;
    for (unsigned frame = 0u; frame < 600u; ++frame) {
        host.now += 150u + next_random(&state) % 400u; /* Beyond the 120 ms tap merge. */
        if (!engine.ui.menu_open && next_random(&state) % 64u == 0u) {
            engine.ui.menu_open = true; /* Shortcut to Settings from the resting scene. */
            engine.ui.page = JELLI_UI_SETTINGS;
        }
        host.pending = audio_input(&engine, &state);
        host.ready = next_random(&state) % 4u != 0u; /* Some frames are idle: coos. */
        uint8_t before = engine.game.sound_level;
        CHECK(jelli_pet_frame(&engine));
        unsigned cue =
            jelli_pet_ui_sound(&engine.ui, &engine.game.pets[engine.game.active], host.now);
        uint8_t level = engine.game.sound_level;
        unsigned volume = cue ? jelli_sound_volume(level, cue - 1u) : 0u;
        CHECK(cue <= JELLI_SOUND_COUNT && volume <= JELLI_SOUND_VOLUME_MAX);
        muted_cues += cue && level == 0u ? 1u : 0u;
        requests += volume ? 1u : 0u;
        if (level == 0u || !cue) /* Muted or silent: no request at all. */
            CHECK(volume == 0u);
        else
            CHECK(volume == jelli_sound_volume(level, cue - 1u) && volume > 0u);
        if (level != before) { /* The press is heard at its new level, not the old one. */
            CHECK(cue == JELLI_SOUND_TAP + 1u);
            CHECK(volume == jelli_sound_volume(level, JELLI_SOUND_TAP));
            CHECK(volume != jelli_sound_volume(before, JELLI_SOUND_TAP));
            ++heard_changes;
        }
    }
    printf("audio walk 0x%08x: %u muted cues silenced, %u requests, %u level changes heard\n",
           (unsigned)seed, muted_cues, requests, heard_changes);
    CHECK(muted_cues >= 5u && requests >= 10u && heard_changes >= 5u);
}

int main(void)
{
    static const uint32_t seeds[] = {1u, 0x5eed50u, 0x0badf00du, 0x13579bdfu, 0xdeadbeefu};
    static World world;
    World total = {0};
    for (size_t s = 0u; s < sizeof(seeds) / sizeof(seeds[0]); ++s) {
        run(seeds[s], &world);
        total.opened += world.opened;
        total.plus_ok += world.plus_ok;
        total.minus_ok += world.minus_ok;
        total.low_refused += world.low_refused;
        total.high_refused += world.high_refused;
        total.clock_opened += world.clock_opened;
        total.sound_frames += world.sound_frames;
        total.full_checks += world.full_checks;
    }
    printf("sound editor walk: %u opens, %u up, %u down, %u/%u refused at 0/10, %u clock opens, "
           "%u editor frames, %u full-redraw comparisons\n",
           total.opened, total.plus_ok, total.minus_ok, total.low_refused, total.high_refused,
           total.clock_opened, total.sound_frames, total.full_checks);
    /* The generator must actually reach every branch it claims to cover. */
    CHECK(total.opened >= 8u && total.plus_ok >= 20u && total.minus_ok >= 20u);
    CHECK(total.clock_opened >= 4u && total.sound_frames >= 200u && total.full_checks >= 100u);
    CHECK(total.low_refused >= 2u && total.high_refused >= 2u);
    host_audio_walk(UINT32_C(0x50a1d001));
    host_audio_walk(UINT32_C(0x0000beef));
    puts("PASS: seeded Settings walks keep one editor, level 0..10, saves, events, redraws; "
         "muted hosts never request audio");
    return 0;
}
