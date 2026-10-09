/* Seeded engine walk with the shipped high-resolution art and all four pet forms
 * (Mint, Lilac, Nirmal and the unicorn in one roster). Inputs arrive through the
 * platform poll queue as on a device: taps on ring slots, MENU, the creature and random
 * points, and swipes; the host changes battery readings and the battery setting, jumps
 * the day (night tint), switches pets, rests and wakes them (sleep particles), offers
 * and completes presents (magic particles), and varies the frame interval. After every
 * frame: the panel equals a full redraw of the same state, no pixel outside the
 * reported damage changed, and stride padding is untouched. Coverage counters prove the
 * walk reached every form by day and night, the battery indicator, both particle kinds
 * and the pages. A failure prints the seed and step; an argument adds soak seeds. */
#include "jelli/pet_engine.h"
#include "../core/pet_canvas.h" /* Round-panel mask for the oracle. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_STEP(expression)                                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x step %u)\n", __FILE__, __LINE__, #expression,  \
                    (unsigned)w.seed, w.step);                                                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { STRIDE = JELLI_WIDTH + 5, PIXELS = STRIDE * JELLI_HEIGHT, SENTINEL = 0x5aa5, STEPS = 450 };
enum { QUEUE = 4 };
static uint16_t pixels[PIXELS], fresh[PIXELS], before[PIXELS];
static unsigned round_left[JELLI_HEIGHT], round_right[JELLI_HEIGHT];
static const JelliAssetSet marker_set; /* Only compared, never read. */

typedef struct {
    unsigned frames, partial, full, forms[JELLI_FORM_COUNT][2], battery, sleep, magic, pages;
} Coverage;

typedef struct {
    JelliPetEngine engine;
    JelliPetUi shadow;
    JelliInput queue[QUEUE];
    unsigned queued, step;
    uint64_t now;
    uint32_t seed, state;
    Coverage seen;
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

static bool poll_input(void *context, JelliInput *input)
{
    (void)context;
    if (!w.queued)
        return false;
    *input = w.queue[0];
    memmove(w.queue, w.queue + 1, --w.queued * sizeof(w.queue[0]));
    return true;
}

static void present(void *context, const JelliSurface *surface)
{
    (void)context;
    CHECK_STEP(surface->pixels == pixels);
}

static void push(JelliInputKind kind, int x, int y)
{
    if (w.queued < QUEUE)
        w.queue[w.queued++] = (JelliInput){kind, x, y};
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

/* The roster holds every form: slot 1's Mint is grown into Lilac before the first frame. */
static void start(uint32_t seed)
{
    memset(&w, 0, sizeof(w));
    w.seed = w.state = seed;
    for (size_t i = 0u; i < PIXELS; ++i)
        pixels[i] = SENTINEL;
    JelliPlatform platform = {.now_ms = clock_ms, .poll = poll_input, .present = present};
    CHECK_STEP(jelli_pet_init(&w.engine, platform,
                              (JelliSurface){pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}}));
    w.engine.game.pets[1].form = JELLI_FORM_LILAC;
    CHECK_STEP(w.engine.game.count == 4u && w.engine.game.active == 3u);
    CHECK_STEP(jelli_game_valid(&w.engine.game));
}

/* Full redraw of the same post-frame UI (a foreign asset pointer defeats every partial
 * scope) into a separate buffer; the panel must match it exactly. */
static void check_full_redraw(void)
{
    w.shadow = w.engine.ui;
    w.shadow.last_view.assets = &marker_set;
    JelliSurface full = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&full, &w.engine.game, &w.shadow, w.engine.animation_ms, w.engine.paused);
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y) {
        size_t row = (size_t)y * STRIDE;
        if (round_right[y] > round_left[y])
            CHECK_STEP(memcmp(&pixels[row + round_left[y]], &fresh[row + round_left[y]],
                              (round_right[y] - round_left[y]) * sizeof(uint16_t)) == 0);
    }
}

/* Outside the damage nothing changed, and padding columns are never written. */
static void check_damage(void)
{
    JelliRect d = w.engine.surface.damage;
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
        for (unsigned x = 0u; x < STRIDE; ++x) {
            size_t at = (size_t)y * STRIDE + x;
            if (x >= JELLI_WIDTH)
                CHECK_STEP(pixels[at] == SENTINEL);
            else if (x < d.x || x >= d.x + d.width || y < d.y || y >= d.y + d.height)
                CHECK_STEP(pixels[at] == before[at]);
        }
}

static void count_coverage(void)
{
    const JelliPetUi *ui = &w.engine.ui;
    const JelliPetRenderKey *v = &ui->last_view;
    const JelliRect d = w.engine.surface.damage;
    w.seen.frames++;
    w.seen.partial += d.width && (d.width < JELLI_WIDTH || d.height < JELLI_HEIGHT) ? 1u : 0u;
    w.seen.full += d.width == JELLI_WIDTH && d.height == JELLI_HEIGHT ? 1u : 0u;
    w.seen.forms[v->form % JELLI_FORM_COUNT][v->night ? 1 : 0]++;
    w.seen.battery += v->battery_shown ? 1u : 0u;
    w.seen.pages |= 1u << ui->page;
    for (unsigned i = 0u; i < JELLI_PARTICLE_CAPACITY; ++i) {
        const JelliParticle *p = &ui->particles.items[i];
        w.seen.sleep += p->life && (p->style & 64u) ? 1u : 0u;
        w.seen.magic += p->life && (p->style & 128u) ? 1u : 0u;
    }
}

static void frame(void)
{
    memcpy(before, pixels, sizeof(pixels));
    CHECK_STEP(jelli_pet_frame(&w.engine));
    CHECK_STEP(jelli_game_valid(&w.engine.game));
    check_damage();
    const JelliRect d = w.engine.surface.damage;
    bool partial = d.width && (d.width < JELLI_WIDTH || d.height < JELLI_HEIGHT);
    if (partial || w.seen.frames % 4u == 0u) /* Every partial frame, a sample of full ones. */
        check_full_redraw();
    count_coverage();
}

static void command(JelliCommandKind kind, uint32_t value)
{
    JelliGame *game = &w.engine.game;
    (void)jelli_game_command(game, (JelliCommand){kind, game->pets[game->active].id, value});
}

/* Player input through the poll queue: ring slots, MENU, the creature, anywhere. */
static void player(unsigned pick)
{
    static const int centers[7][2] = {{110, 111}, {356, 111}, {405, 233}, {356, 355},
                                      {110, 355}, {61, 233},  {233, 420}};
    static const int swipes[4][2] = {{0, 80}, {0, -80}, {80, 0}, {-80, 0}};
    const JelliRect actor = w.engine.ui.actor_bounds;
    JelliPetUiButton b;
    const JelliGame *game = &w.engine.game;
    if (pick < 3u) {
        const int *c = centers[next_random() % 7u];
        push(JELLI_TAP, c[0], c[1]);
    } else if (pick < 6u) { /* Whatever control the current page shows in a random slot. */
        if (jelli_pet_ui_control(&w.engine.ui, next_random() % 7u, game->pets[game->active].asleep,
                                 &b))
            push(JELLI_TAP, (int)(b.bounds.x + b.bounds.width / 2u),
                 (int)(b.bounds.y + b.bounds.height / 2u));
    } else if (pick == 6u) {
        push(JELLI_TAP, (int)(actor.x + actor.width / 2u), (int)(actor.y + actor.height / 2u));
    } else if (pick == 7u) {
        push(JELLI_TAP, (int)(next_random() % JELLI_WIDTH), (int)(next_random() % JELLI_HEIGHT));
    } else {
        const int *s = swipes[next_random() % 4u];
        push(JELLI_SWIPE, s[0], s[1]);
    }
}

/* Host-side and game changes a frame can carry. */
static void host(unsigned pick)
{
    static const uint8_t percents[] = {0u, 5u, 15u, 42u, 100u, JELLI_POWER_PERCENT_UNKNOWN};
    JelliGame *game = &w.engine.game;
    JelliPet *pet = &game->pets[game->active];
    if (pick == 0u) {
        w.engine.ui.power = (JelliPowerReading){(uint8_t)(next_random() % JELLI_POWER_STATE_COUNT),
                                                percents[next_random() % sizeof(percents)]};
    } else if (pick == 1u) {
        game->battery_display = !game->battery_display;
    } else if (pick == 2u) { /* Jump the day: night tint, atmosphere, clock. */
        uint32_t offset = pet->phase_offset;
        pet->phase_offset = next_random() % JELLI_DAY_TICKS;
        if (!jelli_game_valid(game))
            pet->phase_offset = offset;
    } else if (pick == 3u) {
        command(JELLI_CMD_ACTIVATE, game->pets[next_random() % game->count].id);
    } else if (pick == 4u) {
        command(pet->asleep ? JELLI_CMD_WAKE : JELLI_CMD_REST, 0u);
    } else if (pick == 5u) {
        unsigned index = next_random() % JELLI_PRIZE_COUNT;
        if (!game->prizes.offered && !(game->prizes.owned & (1u << index))) {
            game->prizes.offered = (uint8_t)(index + 1u);
            game->prizes.offered_pet = pet->id;
        }
    } else if (pick == 6u) {
        jelli_prize_complete(game, next_random() % JELLI_PRIZE_COUNT);
    } else if (pick == 7u) { /* Open a menu page directly, as the debug interface can. */
        w.engine.ui.menu_open = true;
        w.engine.ui.page = (JelliPetPage)(next_random() % (JELLI_UI_HEALTH + 1u));
    } else if (pick == 8u) { /* Start a health activity when the pet is ready for it. */
        w.engine.ui.menu_open = true;
        (void)jelli_pet_health_select(
            &w.engine.ui, game, (JelliPetUiAction)(JELLI_UI_ACTION_BRUSH + next_random() % 5u));
    } else {
        static const JelliCommandKind care[] = {JELLI_CMD_FEED, JELLI_CMD_PLAY, JELLI_CMD_CLEAN,
                                                JELLI_CMD_CARE, JELLI_CMD_TOUCH};
        command(care[next_random() % 5u], 0u);
    }
}

static void advance(void)
{
    uint32_t pick = next_random() % 64u;
    if (pick < 54u)
        w.now += 16u;
    else if (pick < 63u)
        w.now += 1u + next_random() % 900u;
    else
        w.now += 2000u + next_random() % 60000u; /* Long gaps: sleep timers, fades. */
}

static void run_seed(uint32_t seed, unsigned steps)
{
    start(seed);
    frame();
    for (w.step = 0u; w.step < steps; ++w.step) {
        uint32_t pick = next_random() % 64u; /* Two thirds of the frames carry no input. */
        if (pick < 14u)
            player(w.engine.ui.menu_open && pick >= 9u ? 0u : pick % 12u);
        else if (pick < 24u)
            host(pick - 14u);
        advance();
        frame();
    }
}

static void add(Coverage *total, const Coverage *seen)
{
    total->frames += seen->frames;
    total->partial += seen->partial;
    total->full += seen->full;
    total->battery += seen->battery;
    total->sleep += seen->sleep;
    total->magic += seen->magic;
    total->pages |= seen->pages;
    for (unsigned f = 0u; f < JELLI_FORM_COUNT; ++f)
        for (unsigned n = 0u; n < 2u; ++n)
            total->forms[f][n] += seen->forms[f][n];
}

int main(int argc, char **argv)
{
    static const uint32_t seeds[] = {0x1a2b3c4du, 0x0badf00du, 0x7e57c0deu,
                                     0xc0ffee11u, 0x5eed1e55u, 0x31415926u};
    unsigned pinned = (unsigned)(sizeof(seeds) / sizeof(seeds[0]));
    unsigned extra = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u;
    Coverage total = {0};
    round_spans();
    for (unsigned i = 0u; i < pinned + extra; ++i) {
        run_seed(i < pinned ? seeds[i] : 0x9e3779b9u * (i + 1u), STEPS);
        add(&total, &w.seen);
    }
    w.seed = 0u;
    printf("walk: %u frames (%u partial, %u full); day/night frames per form: %u/%u %u/%u %u/%u "
           "%u/%u; "
           "battery %u; sleep %u and magic %u particle-frames; pages 0x%03x\n",
           total.frames, total.partial, total.full, total.forms[0][0], total.forms[0][1],
           total.forms[1][0], total.forms[1][1], total.forms[2][0], total.forms[2][1],
           total.forms[3][0], total.forms[3][1], total.battery, total.sleep, total.magic,
           total.pages);
    for (unsigned f = 0u; f < JELLI_FORM_COUNT; ++f)
        CHECK_STEP(total.forms[f][0] > 50u && total.forms[f][1] > 50u); /* Day and night. */
    CHECK_STEP(total.partial > 200u && total.battery > 100u);
    CHECK_STEP(total.sleep > 100u && total.magic > 100u);
    CHECK_STEP(total.pages == (1u << JELLI_UI_PAGE_COUNT) - 1u); /* Every page. */
    puts("PASS: high-resolution four-pet walk equals full redraws within the damage");
    return 0;
}
