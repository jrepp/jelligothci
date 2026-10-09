/* Seeded soak of screen sleep through the real engine over hours of injected time (frame,
 * idle, zero, backward, and multi-hour gaps; offline catch-up; single, double, and triple
 * taps; swipes; pauses; debug sleep/wake/tap/swipe/capture/state). The "dark" engine
 * sleeps for real; the "lit" oracle is forced awake and gets only the input the reference
 * gate (screen_gate.c) admits. Each frame: present exactly while awake, one host edge per
 * change, the oracle's sound while awake and silence while dark, a fresh full render at
 * each wake, byte-identical games and event logs, and QUIT always quits. Failures print
 * seed and step; an argument adds seeds. */
#include "jelli/debug.h"
#include "jelli/save.h"
#include "screen_gate.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_STEP(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x step %u now %" PRIu64 ")\n", __FILE__,         \
                    __LINE__, #expression, (unsigned)w.seed, w.step, hosts[0].now);                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STEPS = 1000, QUEUE = 8, DARK = 0, LIT = 1 };

typedef struct {
    uint64_t now;
    unsigned presents, pauses;
    bool paused; /* Last value the pause callback reported. */
    JelliRect damage;
    JelliInput queue[QUEUE];
    unsigned count, head;
} Host;

enum { FRAMES, DARKS, SLEEPS, WAKES, FORCED, GUARDED, CUES, MID_RESUME, TALLIES };

static struct {
    uint32_t seed, state;
    unsigned step, n[TALLIES];
    uint64_t simulated_ms;
    ScreenGate gate;
    int last_x, last_y;
} w;
static Host hosts[2];
static JelliPetEngine engines[2], scratch;
static JelliDebug debugs[2];
static uint16_t pixels[2][JELLI_WIDTH * JELLI_HEIGHT], fresh[JELLI_WIDTH * JELLI_HEIGHT];
static uint8_t encoded[2][JELLI_SAVE_CAPACITY];

static uint32_t next_random(void)
{
    uint32_t x = w.state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w.state = x;
    return x;
}

static uint64_t now_ms(void *context) { return ((Host *)context)->now; }

static bool poll(void *context, JelliInput *input)
{
    Host *host = context;
    if (!host->count)
        return false;
    *input = host->queue[host->head];
    host->head = (host->head + 1u) % QUEUE;
    --host->count;
    return true;
}

static void present(void *context, const JelliSurface *surface)
{
    Host *host = context;
    CHECK_STEP(host != &hosts[DARK] || !engines[DARK].screen.asleep); /* Never while dark. */
    ++host->presents;
    host->damage = surface->damage;
}

static void paused(void *context, bool value)
{
    Host *host = context;
    ++host->pauses;
    host->paused = value;
}

static void push(unsigned slot, JelliInput input)
{
    CHECK_STEP(hosts[slot].count < QUEUE);
    hosts[slot].queue[(hosts[slot].head + hosts[slot].count++) % QUEUE] = input;
}

static void start(uint32_t seed)
{
    memset(&w, 0, sizeof(w));
    w.seed = w.state = seed;
    uint64_t base = next_random() % 2u ? 0u : (uint64_t)next_random() << 20;
    for (unsigned slot = 0u; slot < 2u; ++slot) {
        hosts[slot] = (Host){.now = base};
        debugs[slot] = (JelliDebug){0};
        JelliPlatform platform = {&hosts[slot], now_ms, poll, present, paused};
        JelliSurface surface = {pixels[slot], JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
        CHECK_STEP(jelli_pet_init(&engines[slot], platform, surface));
    }
    screen_gate_init(&w.gate, base);
    w.last_x = w.last_y = 233;
}

static const char *debug_line(unsigned slot, const char *text)
{
    JelliDebug *debug = &debugs[slot];
    debug->reply_size = 0u;
    for (size_t i = 0u; text[i]; ++i)
        jelli_debug_feed(debug, &engines[slot], text[i], hosts[slot].now);
    CHECK_STEP(debug->reply_size > 0u);
    return debug->reply;
}

static void debug_screen_status(void)
{
    char expected[96];
    (void)snprintf(expected, sizeof(expected),
                   "{\"awake\":%s,\"sleep_in_ms\":%" PRIu64 ",\"sleep_after_ms\":180000}",
                   w.gate.dark ? "false" : "true", screen_gate_remaining(&w.gate, hosts[DARK].now));
    CHECK_STEP(strstr(debug_line(DARK, next_random() % 2u ? "@J1 7 screen\n" : "@J1 7 state\n"),
                      expected) != NULL);
}

static void debug_capture(void)
{
    const char *reply = debug_line(DARK, "@J1 8 capture\n");
    if (w.gate.dark) {
        CHECK_STEP(strstr(reply, "\"error\":\"screen_asleep\"") && !debugs[DARK].captured);
        return;
    }
    if (!engines[DARK].ui.rendered) { /* A forced wake has not been drawn yet. */
        CHECK_STEP(strstr(reply, "\"error\":\"busy\"") && !debugs[DARK].captured);
        return;
    }
    CHECK_STEP(debugs[DARK].captured);
    CHECK_STEP(strstr(debug_line(DARK, "@J1 9 screen sleep\n"), "\"error\":\"busy\""));
    char release[48];
    (void)snprintf(release, sizeof(release), "@J1 10 release %" PRIu32 "\n",
                   debugs[DARK].capture_id);
    (void)debug_line(DARK, release);
    CHECK_STEP(!debugs[DARK].captured && !engines[DARK].screen.asleep);
    engines[LIT].last_ms = engines[DARK].last_ms; /* Release forgives the frozen time. */
}

/* Debug input goes to both engines, only while lit: on a dark engine it is hit-tested
 * against the view drawn before sleep (stale layout, ring_moving), unlike the oracle. */
static void debug_input(uint32_t pick)
{
    char line[48];
    if (w.gate.dark)
        return;
    bool resuming = engines[DARK].game.resuming;
    CHECK_STEP(resuming == engines[LIT].game.resuming);
    if (pick % 2u)
        (void)snprintf(line, sizeof(line), "@J1 11 tap %u %u\n", next_random() % 466u,
                       next_random() % 466u);
    else
        (void)snprintf(line, sizeof(line), "@J1 11 swipe %u\n", next_random() % 4u);
    char reply[64];
    (void)snprintf(reply, sizeof(reply), "%s", debug_line(DARK, line));
    CHECK_STEP(strcmp(reply, debug_line(LIT, line)) == 0);
    CHECK_STEP(strstr(reply, resuming ? "\"error\":\"busy\"" : "\"input\":\"delivered\""));
    if (!resuming)
        screen_gate_wake(&w.gate, hosts[DARK].now); /* Lit already: restarts the timer. */
}

static void between_frames(void)
{
    uint32_t pick = next_random() % 400u;
    if (pick < 4u) {
        (void)debug_line(DARK, "@J1 5 screen sleep\n");
        screen_gate_sleep(&w.gate);
    } else if (pick < 7u) {
        (void)debug_line(DARK, "@J1 6 screen wake\n");
        w.n[FORCED] += w.gate.dark;
        screen_gate_wake(&w.gate, hosts[DARK].now);
    } else if (pick < 11u) {
        debug_input(pick);
    } else if (pick < 14u) {
        debug_screen_status();
    } else if (pick < 16u) {
        debug_capture();
    } else if (pick == 16u) { /* Host restored a snapshot after time offline. */
        uint64_t offline = (uint64_t)(next_random() % 7200u) * 1000u;
        jelli_game_resume_begin(&engines[DARK].game, offline);
        jelli_game_resume_begin(&engines[LIT].game, offline);
    }
}

/* Mostly straddles the 80 px radius around the previous tap. */
static int near_or_random(int previous)
{
    bool near = next_random() % 8u < 5u;
    return near ? previous + (int)(next_random() % 201u) - 100 : (int)(next_random() % 466u);
}

/* Queues the frame's polled input on the dark host; admitted ones also go to the oracle. */
static void queue_inputs(void)
{
    uint32_t pick = next_random() % 16u;
    unsigned count = pick < 7u ? 0u : pick < 13u ? 1u : 2u + next_random() % 3u;
    if (w.gate.redraw) /* After a forced wake, input polled before the redraw is judged */
        count = 0u;    /* against the pre-sleep view as well; see the stale-view case. */
    bool woke = false;
    for (unsigned i = 0u; i < count; ++i) {
        uint32_t kind = next_random() % 16u;
        JelliInput input;
        if (kind < 12u) {
            w.last_x = near_or_random(w.last_x < 0 || w.last_x > 465 ? 233 : w.last_x);
            w.last_y = near_or_random(w.last_y < 0 || w.last_y > 465 ? 233 : w.last_y);
            input = (JelliInput){JELLI_TAP, w.last_x, w.last_y};
        } else if (kind < 15u) {
            static const int swipes[4][2] = {{0, -80}, {0, 80}, {-80, 0}, {80, 0}};
            uint32_t d = next_random() % 4u;
            input = (JelliInput){JELLI_SWIPE, swipes[d][0], swipes[d][1]};
        } else {
            input = (JelliInput){JELLI_TOGGLE_PAUSE, 0, 0};
        }
        /* A swipe polled after the waking tap, before the waking frame draws, is judged
         * against the pre-sleep view (last_view.ring_moving); see the stale-view case. */
        if (woke && input.kind == JELLI_SWIPE)
            continue;
        push(DARK, input);
        bool was_dark = w.gate.dark;
        if (screen_gate_admits(&w.gate, input, hosts[DARK].now))
            push(LIT, input);
        else if (!was_dark)
            ++w.n[GUARDED];
        woke |= was_dark && !w.gate.dark;
        w.n[WAKES] += was_dark && !w.gate.dark;
    }
}

static void advance_clock(void)
{
    uint32_t pick = next_random() % 1000u;
    uint64_t step = 16u + next_random() % 85u, now = hosts[0].now;
    if (pick < 50u)
        step = 1000u + (uint64_t)(next_random() % 400u) * 1000u; /* Idle across the rule. */
    else if (pick < 53u)
        step = (uint64_t)(1u + next_random() % 4u) * 3600000u; /* Host suspended for hours. */
    else if (pick < 60u)
        step = 0u;
    else if (pick < 70u)
        step = next_random() % 2u ? 499u + next_random() % 3u : 179999u + next_random() % 3u;
    bool back = pick >= 70u && pick < 80u; /* A regressing host clock. */
    w.simulated_ms += back ? 0u : step;
    now = !back ? now + step : now > 300u ? now - 1u - next_random() % 300u : 0u;
    hosts[DARK].now = hosts[LIT].now = now;
}

static void compare_engines(void)
{
    const JelliPetEngine *a = &engines[DARK], *b = &engines[LIT];
    const JelliGame *ga = &a->game, *gb = &b->game; /* Also mid-resume, which saves refuse. */
    CHECK_STEP(ga->resuming == gb->resuming && ga->ticks == gb->ticks);
    CHECK_STEP(ga->revision == gb->revision && ga->backlog_ms == gb->backlog_ms);
    CHECK_STEP(ga->resume_remaining_ms == gb->resume_remaining_ms);
    for (unsigned i = 0u; i < JELLI_PET_CAPACITY; ++i) {
        const JelliPet *pa = &ga->pets[i], *pb = &gb->pets[i];
        CHECK_STEP(pa->ticks == pb->ticks && !memcmp(pa->needs, pb->needs, sizeof(pa->needs)));
    }
    JelliSave save = {.game = a->game, .sequence = 1u}; /* Refused mid-resume or backlogged. */
    size_t size = jelli_save_encode(&save, encoded[0], JELLI_SAVE_CAPACITY);
    save.game = b->game;
    CHECK_STEP(size == jelli_save_encode(&save, encoded[1], JELLI_SAVE_CAPACITY));
    CHECK_STEP(memcmp(encoded[0], encoded[1], size) == 0);
    w.n[MID_RESUME] += !size;
    CHECK_STEP(a->animation_ms == b->animation_ms && a->paused == b->paused);
    CHECK_STEP(a->events.sequence == b->events.sequence && a->ui.page == b->ui.page);
    CHECK_STEP(memcmp(a->events.items, b->events.items, sizeof(a->events.items)) == 0);
    CHECK_STEP(hosts[DARK].pauses == hosts[LIT].pauses && hosts[DARK].paused == a->paused);
    CHECK_STEP(a->ui.menu_open == b->ui.menu_open && a->ui.save_requested == b->ui.save_requested);
}

/* The waking frame must equal a fresh full render of the same state. */
static void check_wake_frame(void)
{
    JelliRect d = hosts[DARK].damage;
    CHECK_STEP(!d.x && !d.y && d.width == JELLI_WIDTH && d.height == JELLI_HEIGHT);
    scratch = engines[DARK];
    scratch.ui.rendered = false;
    scratch.surface.pixels = fresh;
    jelli_pet_render(&scratch.surface, &scratch.game, &scratch.ui, scratch.animation_ms,
                     scratch.paused);
    CHECK_STEP(memcmp(fresh, pixels[DARK], sizeof(fresh)) == 0);
}

/* At a wake every input-driven UI field matches the oracle; then the oracle adopts the
 * dark engine's render history so later taps hit the same drawn targets. Pose and hit
 * area may differ after a dark period: animation anchors reset only while drawing
 * (pet_timing.c), and a paused forced redraw re-reads animation_ms (pet_render.c). */
static void adopt_drawn_view(void)
{
    const JelliPetUi *a = &engines[DARK].ui;
    JelliPetUi *b = &engines[LIT].ui;
    /* Reward bookkeeping is event-driven; only its reveal animation advances while drawing. */
    const JelliPetRewards *ra = &a->rewards, *rb = &b->rewards;
    CHECK_STEP(ra->cursor == rb->cursor && ra->pet_id == rb->pet_id);
    CHECK_STEP(ra->completed == rb->completed && ra->command_value == rb->command_value);
    CHECK_STEP(ra->activity == rb->activity && ra->command == rb->command);
    CHECK_STEP(ra->health_kind == rb->health_kind && ra->pending == rb->pending);
    CHECK_STEP(ra->health == rb->health && memcmp(ra->gains, rb->gains, sizeof(ra->gains)) == 0);
    CHECK_STEP(memcmp(&a->tunables, &b->tunables, sizeof(a->tunables)) == 0);
    CHECK_STEP(a->page == b->page && a->menu_open == b->menu_open);
    CHECK_STEP(a->clock_edit == b->clock_edit && a->sound_edit == b->sound_edit);
    CHECK_STEP(a->clicker_pet == b->clicker_pet && a->clicker_hits == b->clicker_hits);
    CHECK_STEP(a->clicker_goal == b->clicker_goal && a->clicker_stage == b->clicker_stage);
    CHECK_STEP(a->clicker_done == b->clicker_done && a->routine_random == b->routine_random);
    CHECK_STEP(a->latched_prize == b->latched_prize && a->catch_seen == b->catch_seen);
    CHECK_STEP(a->highlighted_prize == b->highlighted_prize);
    CHECK_STEP(a->catch_anchor_ms == b->catch_anchor_ms);
    CHECK_STEP(a->save_requested == b->save_requested && a->save_status == b->save_status);
    CHECK_STEP(a->sound_pending == b->sound_pending && a->sound_played == b->sound_played);
    CHECK_STEP(a->last_sound_ms == b->last_sound_ms && a->coo_anchor_ms == b->coo_anchor_ms);
    CHECK_STEP(a->coo_pet == b->coo_pet && a->clock_known == b->clock_known);
    CHECK_STEP(a->timezone_minutes == b->timezone_minutes && a->clock_adjust == b->clock_adjust);
    *b = *a;
}

static void frame(void)
{
    unsigned presents = hosts[DARK].presents;
    jelli_pet_screen_wake(&engines[LIT], hosts[LIT].now); /* The oracle never sleeps. */
    CHECK_STEP(jelli_pet_frame(&engines[DARK]) && jelli_pet_frame(&engines[LIT]));
    w.n[SLEEPS] += screen_gate_idle(&w.gate, hosts[DARK].now);
    CHECK_STEP(hosts[DARK].count == 0u && hosts[LIT].count == 0u);
    CHECK_STEP(jelli_pet_screen_awake(&engines[DARK]) == !w.gate.dark);
    CHECK_STEP(hosts[DARK].presents == presents + (w.gate.dark ? 0u : 1u));
    bool woke = w.gate.redraw && !w.gate.dark;
    if (woke)
        check_wake_frame();
    w.gate.redraw = false;
    bool awake = !w.gate.reported_dark, changed = jelli_pet_screen_changed(&engines[DARK], &awake);
    CHECK_STEP(changed == (w.gate.dark != w.gate.reported_dark) && awake == !w.gate.dark);
    w.gate.reported_dark = w.gate.dark;
    unsigned dark_cue = jelli_pet_sound_cue(&engines[DARK], hosts[DARK].now);
    unsigned lit_cue = jelli_pet_sound_cue(&engines[LIT], hosts[LIT].now);
    CHECK_STEP(dark_cue == (w.gate.dark ? 0u : lit_cue)); /* Silent dark; no burst on wake. */
    if (woke)
        adopt_drawn_view();
    w.n[CUES] += dark_cue != 0u;
    w.n[DARKS] += w.gate.dark;
    ++w.n[FRAMES];
}

static void run_seed(uint32_t seed, bool pinned)
{
    start(seed);
    frame();
    for (w.step = 0u; w.step < STEPS; ++w.step) {
        advance_clock();
        between_frames();
        queue_inputs();
        frame();
        if (w.step % 64u == 0u)
            compare_engines();
        /* Both games stay valid (touching a recovering pet once broke this). */
        CHECK_STEP(jelli_game_valid(&engines[DARK].game) && jelli_game_valid(&engines[LIT].game));
    }
    compare_engines();
    /* QUIT still quits, whichever state the screen is in and whatever precedes it. */
    push(DARK, (JelliInput){JELLI_TAP, w.last_x, w.last_y});
    push(DARK, (JelliInput){JELLI_QUIT, 0, 0});
    CHECK_STEP(!jelli_pet_frame(&engines[DARK]) && !engines[DARK].running);
    CHECK_STEP(!pinned || (w.n[WAKES] >= 3u && w.n[SLEEPS] >= 10u && w.n[FORCED] >= 1u));
}

int main(int argc, char **argv)
{
    static const uint32_t pinned[] = {0x5851f42du, 0x44b31596u, 0x2b7b93efu, 0x792ed2a5u};
    unsigned extra = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u;
    unsigned n[TALLIES] = {0};
    uint64_t simulated = 0u;
    for (unsigned i = 0u; i < 4u + extra; ++i) {
        run_seed(i < 4u ? pinned[i] : 0x3c6ef372u ^ ((i + 64u) * 0x9e3779b9u), i < 4u);
        for (unsigned k = 0u; k < TALLIES; ++k)
            n[k] += w.n[k];
        simulated += w.simulated_ms;
    }
    printf("screen soak: %u frames (%u dark) over %.1f simulated hours, %u idle sleeps, %u "
           "double-tap wakes, %u debug wakes, %u guarded taps, %u cues, %u mid-resume "
           "comparisons\n",
           n[FRAMES], n[DARKS], (double)simulated / 3600000.0, n[SLEEPS], n[WAKES], n[FORCED],
           n[GUARDED], n[CUES], n[MID_RESUME]);
    return 0;
}
