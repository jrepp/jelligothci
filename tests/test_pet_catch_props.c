/* Seeded property tests for the bobbing-present partial redraw. The real pet engine runs
 * on a fake clock while random inputs land in the same frames as bob steps: pose, night
 * and minute changes, touches, stat swipes, battery readings and the display toggle,
 * save status and result captions, offering, catching, latching and giving presents,
 * reward claims, menu and ring transitions, tunables, pause and resume, sleep, travel and
 * pet switches. After every frame the panel equals a full redraw of the same state, the
 * stored render key equals a freshly computed one (no drawn field escapes the key), and a
 * frame whose only change is the bob repaints just the present's rectangle. A failure
 * prints the seed and step to replay; test_pet_catch_redraw.c covers the geometry. */
#include "jelli/pet_engine.h"
#include "../core/pet_canvas.h" /* Internal round-panel mask under test. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_STEP(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x step %u action %u)\n", __FILE__, __LINE__,     \
                    #expression, (unsigned)w.seed, w.step, w.action);                              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT, SENTINEL = 0xa55a, STEPS = 400 };
static uint16_t pixels[PIXELS], fresh[PIXELS], before[PIXELS];
/* The present's fixed redraw rectangle (core/pet_gallery.c, jelli_pet_gallery_catch_bounds). */
static const JelliRect catch_rect = {194u, 66u, 79u, 84u};
static unsigned round_left[JELLI_HEIGHT], round_right[JELLI_HEIGHT];
static const JelliAssetSet marker_set; /* Never dereferenced: only compared. */

typedef struct {
    JelliPetEngine engine;
    JelliPetUi shadow;
    uint64_t now;
    uint32_t seed, state;
    unsigned step, action, catch_only, catch_partial, fulls, frames;
    bool oracle; /* Full-redraw comparison each frame; the long idle runs sample it. */
} World;
static World w;

static uint32_t next_random(void)
{
    uint32_t x = w.state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w.state = x;
    return x;
}

static uint64_t clock_ms(void *context)
{
    (void)context;
    return w.now;
}

static void present(void *context, const JelliSurface *surface)
{
    (void)context;
    CHECK_STEP(surface->pixels == pixels);
}

static bool same_rect(JelliRect a, JelliRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static bool inside(JelliRect rect, unsigned x, unsigned y)
{
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

static bool full_damage(const JelliSurface *surface)
{
    return surface->damage.width == JELLI_WIDTH && surface->damage.height == JELLI_HEIGHT;
}

static void round_spans(void)
{
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y) {
        round_left[y] = JELLI_WIDTH;
        round_right[y] = 0u;
        for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
            if (jelli_canvas_in_round((int)x, (int)y)) {
                round_left[y] = x < round_left[y] ? x : round_left[y];
                round_right[y] = x + 1u;
            }
    }
}

/* Render keys compare field by field, so struct padding never counts. The asset pointer
 * is skipped: the oracle replaces it. Extend this list when the key gains a field. */
#define DIFF(field) (unsigned)(a->field != b->field)
static bool same_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b, bool ignore_catch)
{
    unsigned d =
        DIFF(phase) + DIFF(night) + DIFF(mood) + DIFF(reaction) + DIFF(care_blocked) +
        DIFF(ring_page) + DIFF(ring_visible) + DIFF(ring_moving) + DIFF(clock_edit) +
        DIFF(ring_clock_edit) + DIFF(sound_edit) + DIFF(ring_sound_edit) + DIFF(sound_level) +
        DIFF(unavailable) + DIFF(stat_value) + DIFF(sleep_score) + DIFF(prize_owned) +
        DIFF(prize_discovered) + DIFF(reward_index) + DIFF(offered_prize) + DIFF(latched_prize) +
        DIFF(highlighted_prize) + DIFF(reward_active) + DIFF(timezone_minutes) + DIFF(tile_phase) +
        DIFF(stat_index) + DIFF(clicker_hits) + DIFF(clicker_goal) + DIFF(clicker_stage) +
        DIFF(clicker_done) + DIFF(menu_open) + DIFF(clock_known) + DIFF(clock_minute) +
        DIFF(minute) + DIFF(active_id) + DIFF(stored_id) + DIFF(day) + DIFF(result) + DIFF(bond) +
        DIFF(food) + DIFF(gifts) + DIFF(active) + DIFF(count) + DIFF(page) + DIFF(save_status) +
        DIFF(form) + DIFF(location) + DIFF(health) + DIFF(activity) + DIFF(stored_form) +
        DIFF(bedtime) + DIFF(asleep) + DIFF(stored_asleep) + DIFF(reward_pending) +
        DIFF(reward_claimed) + DIFF(time_unavailable) + DIFF(paused) + DIFF(resuming) +
        DIFF(battery.state) + DIFF(battery.percent) + DIFF(battery_shown) + DIFF(battery_display);
    d += (unsigned)(memcmp(a->prize_origins, b->prize_origins, sizeof(a->prize_origins)) != 0);
    d += (unsigned)(memcmp(a->needs, b->needs, sizeof(a->needs)) != 0);
    return d == 0u && (ignore_catch || a->catch_phase == b->catch_phase);
}
#undef DIFF

static void start(uint32_t seed)
{
    memset(&w, 0, sizeof(w));
    w.seed = w.state = seed;
    w.oracle = true;
    for (size_t i = 0u; i < PIXELS; ++i)
        pixels[i] = SENTINEL;
    JelliPlatform platform = {.now_ms = clock_ms, .present = present};
    CHECK_STEP(jelli_pet_init(&w.engine, platform,
                              (JelliSurface){pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}}));
}

/* Oracle: the same post-frame UI forced through a full redraw (a foreign asset pointer in
 * the stored key defeats every partial scope) into a separate buffer. Ring and tile
 * transitions keep their anchors, so moving frames compare too. */
static void check_full_redraw(void)
{
    w.shadow = w.engine.ui;
    w.shadow.last_view.assets = &marker_set;
    for (size_t i = 0u; i < PIXELS; ++i)
        fresh[i] = SENTINEL;
    JelliSurface full = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&full, &w.engine.game, &w.shadow, w.engine.animation_ms, w.engine.paused);
    CHECK_STEP(full_damage(&full));
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y) {
        size_t row = (size_t)y * STRIDE;
        for (unsigned x = JELLI_WIDTH; x < STRIDE; ++x) /* Stride padding is never written. */
            CHECK_STEP(pixels[row + x] == SENTINEL);
        if (round_right[y] > round_left[y])
            CHECK_STEP(memcmp(&pixels[row + round_left[y]], &fresh[row + round_left[y]],
                              (round_right[y] - round_left[y]) * sizeof(uint16_t)) == 0);
    }
    /* A skipped frame must never hide a drawn field that same_render_key ignores. */
    CHECK_STEP(same_key(&w.shadow.last_view, &w.engine.ui.last_view, false));
}

/* A bob-only frame repaints at most the present (plus live particles) and nothing else. */
static void check_scope(const JelliPetRenderKey *previous, unsigned particles_before)
{
    const JelliSurface *surface = &w.engine.surface;
    const JelliPetRenderKey *view = &w.engine.ui.last_view;
    bool bob_only = previous->catch_phase != view->catch_phase && same_key(previous, view, true);
    bool quiet = !particles_before && !jelli_particles_count(&w.engine.ui.particles);
    w.fulls += full_damage(surface) ? 1u : 0u;
    if (bob_only) {
        ++w.catch_only;
        CHECK_STEP(!full_damage(surface));
        CHECK_STEP(view->offered_prize && !view->menu_open);
    }
    if (quiet && same_rect(surface->damage, catch_rect)) {
        ++w.catch_partial;
        CHECK_STEP(bob_only); /* The rectangle alone is never chosen for any other change. */
        for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
            for (unsigned x = 0u; x < STRIDE; ++x)
                if (!inside(catch_rect, x, y))
                    CHECK_STEP(pixels[y * STRIDE + x] == before[y * STRIDE + x]);
    }
    if (bob_only && quiet)
        CHECK_STEP(same_rect(surface->damage, catch_rect));
}

static void frame(void)
{
    JelliPetRenderKey previous = w.engine.ui.last_view;
    bool rendered = w.engine.ui.rendered;
    unsigned particles = jelli_particles_count(&w.engine.ui.particles);
    memcpy(before, pixels, sizeof(pixels));
    CHECK_STEP(jelli_pet_frame(&w.engine));
    ++w.frames;
    CHECK_STEP(jelli_game_valid(&w.engine.game));
    if (w.oracle || w.frames % 16u == 1u)
        check_full_redraw();
    if (rendered)
        check_scope(&previous, particles);
}

static void tap(int x, int y) { jelli_pet_ui_tap(&w.engine.ui, &w.engine.game, x, y); }

static void tap_control(unsigned slot)
{
    JelliPetUiButton b;
    const JelliGame *game = &w.engine.game;
    if (jelli_pet_ui_control(&w.engine.ui, slot, game->pets[game->active].asleep, &b))
        tap((int)(b.bounds.x + b.bounds.width / 2u), (int)(b.bounds.y + b.bounds.height / 2u));
}

static void command(JelliCommandKind kind, uint32_t value)
{
    JelliGame *game = &w.engine.game;
    (void)jelli_game_command(game, (JelliCommand){kind, game->pets[game->active].id, value});
}

/* Offers an unowned present directly so most of the run has one bobbing. */
static void offer(void)
{
    JelliPrizes *prizes = &w.engine.game.prizes;
    unsigned index = next_random() % JELLI_PRIZE_COUNT;
    if (prizes->offered || (prizes->owned & (1u << index)))
        return;
    prizes->offered = (uint8_t)(index + 1u);
    prizes->offered_pet = w.engine.game.pets[w.engine.game.active].id;
}

/* Host-side state a frame can carry alongside a bob step. */
static void host_change(unsigned pick)
{
    JelliPetUi *ui = &w.engine.ui;
    JelliGame *game = &w.engine.game;
    JelliPet *pet = &game->pets[game->active];
    if (pick == 0u) {
        ui->power = (JelliPowerReading){(uint8_t)(next_random() % 6u), (uint8_t)next_random()};
    } else if (pick == 1u) {
        game->battery_display = !game->battery_display;
    } else if (pick == 2u) {
        ui->save_status = (uint8_t)(next_random() % 4u);
    } else if (pick == 3u) {
        ui->result = (JelliResult)(next_random() % 7u);
    } else if (pick == 4u) {
        w.engine.paused = !w.engine.paused;
    } else if (pick == 5u && !game->resuming) {
        jelli_game_resume_begin(game, 1000u + next_random() % 600000u);
    } else if (pick == 6u) {
        uint32_t offset = pet->phase_offset; /* Jump the day: night, atmosphere, minute. */
        pet->phase_offset = next_random() % JELLI_DAY_TICKS;
        if (!jelli_game_valid(game))
            pet->phase_offset = offset;
    } else if (pick == 7u) {
        unsigned owned = game->prizes.owned, index = next_random() % JELLI_PRIZE_COUNT;
        ui->latched_prize = (owned & (1u << index)) ? (uint8_t)(index + 1u) : 0u;
    } else if (pick == 8u) {
        uint16_t *need = &pet->needs[next_random() % JELLI_NEED_COUNT]; /* Stat tile, mood. */
        *need = (uint16_t)(*need < 1000u ? *need + 1u : *need - 1u);
    } else if (pick == 9u) {
        (void)jelli_tunable_set(&ui->tunables, 0u, JELLI_TUNE_IDLE_MS,
                                400u + next_random() % 1600u);
    } else {
        (void)jelli_tunable_set(&ui->tunables, 0u, JELLI_TUNE_ANIMATION_SCALE,
                                25u + next_random() % 400u);
    }
}

/* Inputs and game commands, as a player or the debug interface would send them. */
static void player_change(unsigned pick)
{
    static const int swipes[4][2] = {{0, 80}, {0, -80}, {80, 0}, {-80, 0}};
    const JelliPetUi *ui = &w.engine.ui;
    const JelliGame *game = &w.engine.game;
    if (pick == 0u) {
        tap(233, 110 + (int)(next_random() % 30u) - 15); /* Catch, or give a held present. */
    } else if (pick == 1u) {
        tap((int)(ui->actor_bounds.x + ui->actor_bounds.width / 2u),
            (int)(ui->actor_bounds.y + ui->actor_bounds.height / 2u));
    } else if (pick <= 3u) {
        tap_control(next_random() % 10u);
    } else if (pick == 4u) {
        const int *s = swipes[next_random() % 4u];
        jelli_pet_ui_swipe(&w.engine.ui, &w.engine.game, s[0], s[1]);
    } else if (pick == 5u) {
        jelli_pet_ui_back(&w.engine.ui);
    } else if (pick == 6u) {
        command(game->pets[game->active].asleep ? JELLI_CMD_WAKE : JELLI_CMD_REST, 0u);
    } else if (pick == 7u) {
        command(JELLI_CMD_ACTIVATE, game->pets[next_random() % game->count].id);
    } else if (pick == 8u) {
        command(JELLI_CMD_TRAVEL, next_random() % 2u);
    } else if (pick == 9u) {
        static const JelliCommandKind care[] = {JELLI_CMD_FEED, JELLI_CMD_PLAY, JELLI_CMD_CLEAN,
                                                JELLI_CMD_CARE, JELLI_CMD_GIFT, JELLI_CMD_CLAIM,
                                                JELLI_CMD_TOUCH};
        command(care[next_random() % 7u], 0u);
    } else {
        jelli_prize_complete(&w.engine.game, next_random() % 9u);
    }
}

/* Advances the clock: mostly a 16 ms frame, sometimes a long gap, sometimes exactly onto
 * the next bob step so other changes coincide with it. */
static void advance(void)
{
    uint32_t pick = next_random() % 8u;
    const JelliPetUi *ui = &w.engine.ui;
    uint64_t animation = w.engine.animation_ms;
    if (pick < 5u)
        w.now += 16u;
    else if (pick == 5u)
        w.now += 1u + next_random() % 400u;
    else if (animation >= ui->catch_anchor_ms)
        w.now += 180u - (animation - ui->catch_anchor_ms) % 180u;
    else
        w.now += 180u;
}

static void run_seed(uint32_t seed, bool pinned)
{
    start(seed);
    frame();
    for (w.step = 0u; w.step < STEPS; ++w.step) {
        uint32_t pick = next_random() % 64u; /* About half the frames carry no input. */
        w.action = pick;
        if (pick < 7u)
            offer();
        if (pick >= 40u && pick < 46u) { /* Leave menus and pauses so the bob resumes. */
            if (w.engine.ui.menu_open)
                jelli_pet_ui_back(&w.engine.ui);
            w.engine.paused = false;
        } else if (pick >= 7u && pick < 18u)
            host_change(pick - 7u);
        else if (pick >= 18u && pick < 30u)
            player_change(pick - 18u);
        advance();
        frame();
    }
    CHECK_STEP(!pinned || (w.catch_only >= 10u && w.catch_partial >= 5u)); /* Reached them. */
}

/* Fixed seeds in CI; an optional argument adds that many more for a soak run. The first
 * seed replaced 0x9e3779b9 when the unicorn became the starting pet: that walk now
 * reaches only two catch-rect partials, below the coverage each pinned seed must show. */
static void random_frames_match_full_redraws(unsigned extra)
{
    static const uint32_t seeds[] = {0xb7e15162u, 0x00c0ffeeu, 0xa66bdc94u, 0x2f49c7b0u};
    unsigned count = (unsigned)(sizeof(seeds) / sizeof(seeds[0])), catch_only = 0u;
    unsigned partial = 0u, fulls = 0u, frames = 0u;
    for (unsigned i = 0u; i < count + extra; ++i) {
        run_seed(i < count ? seeds[i] : 0x6a09e667u ^ (i * 0x9e3779b9u), i < count);
        catch_only += w.catch_only;
        partial += w.catch_partial;
        fulls += w.fulls;
        frames += w.frames;
    }
    printf("random: %u frames, %u bob-only, %u catch-rect partials, %u full redraws\n", frames,
           catch_only, partial, fulls);
}

/* With a present offered and no input, 20 s of 16 ms frames repaint the full panel only
 * for changes other than the bob (pose steps, minute ticks), never for a bob step. */
static void idle_bob_keeps_full_redraws_at_the_pose_rate(void)
{
    for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form)
        for (unsigned asleep = 0u; asleep < 2u; ++asleep) {
            start(0x51ed270bu + form * 2u + asleep);
            JelliGame *game = &w.engine.game;
            JelliPet *pet = &game->pets[game->active];
            pet->form = form;
            if (asleep) {
                pet->asleep = true;
                pet->nap_due = 360000u;
            }
            game->battery_display = ((form + asleep) & 1u) != 0u; /* Both, asleep or awake. */
            w.engine.ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 15u};
            game->prizes.offered = 6u;
            game->prizes.offered_pet = pet->id;
            CHECK_STEP(jelli_game_valid(game));
            unsigned changes = 0u, poses = 0u;
            w.oracle = false;
            frame();
            for (w.step = 0u; w.now < 20000u; ++w.step) {
                JelliPetRenderKey previous = w.engine.ui.last_view;
                w.now += 16u;
                frame();
                const JelliPetRenderKey *view = &w.engine.ui.last_view;
                changes += same_key(&previous, view, true) ? 0u : 1u;
                poses += previous.phase != view->phase ? 1u : 0u;
            }
            CHECK_STEP(game->prizes.offered == 6u && w.catch_only >= 90u);
            CHECK_STEP(w.fulls <= changes && w.fulls <= poses + 2u); /* The pose-change rate. */
            printf("form %u asleep %u: %u frames, %u bob steps, %u pose steps, %u other "
                   "changes, %u full redraws\n",
                   (unsigned)form, asleep, w.frames, w.catch_only, poses, changes, w.fulls);
        }
}

int main(int argc, char **argv)
{
    round_spans();
    idle_bob_keeps_full_redraws_at_the_pose_rate();
    random_frames_match_full_redraws(argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u);
    puts("Catch redraw property checks passed");
    return 0;
}
