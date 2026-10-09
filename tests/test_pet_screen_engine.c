/* Screen sleep through the pet engine with a fake host and injected time. */
#include "jelli/pet_engine.h"
#include "jelli/save.h"
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

typedef struct {
    uint64_t now;
    unsigned presents;
    JelliRect damage;
    JelliInput queue[8];
    unsigned head, count;
} Fake;

static uint16_t pixels[2][JELLI_WIDTH * JELLI_HEIGHT], copy[JELLI_WIDTH * JELLI_HEIGHT];
static JelliPetEngine engines[2];
static Fake fakes[2];
static uint8_t encoded[2][JELLI_SAVE_CAPACITY];

static uint64_t now_ms(void *context) { return ((Fake *)context)->now; }

static bool poll(void *context, JelliInput *input)
{
    Fake *fake = context;
    if (!fake->count)
        return false;
    *input = fake->queue[fake->head];
    fake->head = (fake->head + 1u) % 8u;
    --fake->count;
    return true;
}

static void present(void *context, const JelliSurface *surface)
{
    Fake *fake = context;
    ++fake->presents;
    fake->damage = surface->damage;
}

static void push(Fake *fake, JelliInputKind kind, int x, int y)
{
    CHECK(fake->count < 8u);
    fake->queue[(fake->head + fake->count++) % 8u] = (JelliInput){kind, x, y};
}

/* Engine `slot` with its fake host, started at `start` milliseconds. */
static JelliPetEngine *start_engine(unsigned slot, uint64_t start)
{
    fakes[slot] = (Fake){.now = start};
    JelliPlatform platform = {&fakes[slot], now_ms, poll, present, NULL};
    JelliSurface surface = {pixels[slot], JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    CHECK(jelli_pet_init(&engines[slot], platform, surface));
    CHECK(jelli_pet_frame(&engines[slot]));
    return &engines[slot];
}

static bool frame_at(JelliPetEngine *engine, uint64_t now)
{
    ((Fake *)engine->platform.ctx)->now = now;
    return jelli_pet_frame(engine);
}

static bool full_damage(JelliRect damage)
{
    return damage.x == 0u && damage.y == 0u && damage.width == JELLI_WIDTH &&
           damage.height == JELLI_HEIGHT;
}

static void idle_sleep_and_edge(void)
{
    JelliPetEngine *engine = start_engine(0u, 0u);
    Fake *fake = &fakes[0];
    bool awake = false;
    CHECK(frame_at(engine, 179999u) && jelli_pet_screen_awake(engine));
    CHECK(fake->presents == 2u && !jelli_pet_screen_changed(engine, &awake));
    CHECK(frame_at(engine, 180000u) && !jelli_pet_screen_awake(engine));
    CHECK(fake->presents == 2u); /* The frame that falls asleep already skips present. */
    CHECK(jelli_pet_screen_changed(engine, &awake) && !awake);
    CHECK(!jelli_pet_screen_changed(engine, &awake)); /* Edge-triggered: reported once. */
    for (uint64_t now = 180100u; now < 400000u; now += 100u)
        CHECK(frame_at(engine, now));
    CHECK(fake->presents == 2u && !jelli_pet_screen_awake(engine));
    /* A forced sleep and wake between two host queries coalesce into no change. */
    jelli_pet_screen_wake(engine, 400000u);
    jelli_pet_screen_sleep(engine, 400000u);
    CHECK(!jelli_pet_screen_changed(engine, &awake));
    CHECK(!jelli_pet_screen_changed(NULL, &awake) && !jelli_pet_screen_awake(NULL));
    push(fake, JELLI_QUIT, 0, 0); /* QUIT still quits while dark. */
    CHECK(!frame_at(engine, 400100u) && !engine->running);
}

static void every_input_kind_is_activity(void)
{
    static const JelliInputKind kinds[] = {JELLI_TAP, JELLI_SWIPE, JELLI_TOGGLE_PAUSE};
    for (unsigned i = 0u; i < 3u; ++i) {
        JelliPetEngine *engine = start_engine(0u, 0u);
        push(&fakes[0], kinds[i], kinds[i] == JELLI_SWIPE ? -80 : 0, 0);
        CHECK(frame_at(engine, 100000u));
        CHECK(frame_at(engine, 279999u) && jelli_pet_screen_awake(engine));
        CHECK(frame_at(engine, 280000u) && !jelli_pet_screen_awake(engine)); /* Even paused. */
    }
}

static size_t encode(const JelliGame *game, uint8_t *out)
{
    JelliSave save = {.game = *game, .sequence = 1u};
    size_t size = jelli_save_encode(&save, out, JELLI_SAVE_CAPACITY);
    CHECK(size > 0u);
    return size;
}

/* A dark screen changes nothing about the simulation; it only stops frames and cues. */
static void game_runs_identically_while_asleep(void)
{
    JelliPetEngine *dark = start_engine(0u, 0u), *lit = start_engine(1u, 0u);
    unsigned dark_cues = 0u, lit_cues = 0u, dark_presents = 0u;
    for (uint64_t now = 500u; now <= 900000u; now += 500u) {
        jelli_pet_screen_wake(lit, now); /* Developer activity keeps one screen lit. */
        CHECK(frame_at(dark, now) && frame_at(lit, now));
        unsigned cue = jelli_pet_sound_cue(dark, now);
        dark_cues += cue && now >= 180000u ? 1u : 0u; /* Only cues once the screen is dark. */
        lit_cues += jelli_pet_sound_cue(lit, now) ? 1u : 0u;
        if (now == 180000u)
            dark_presents = fakes[0].presents;
    }
    CHECK(!jelli_pet_screen_awake(dark) && jelli_pet_screen_awake(lit));
    CHECK(fakes[0].presents == dark_presents && fakes[1].presents > dark_presents + 1000u);
    CHECK(dark->game.ticks > 8000u && dark->game.ticks == lit->game.ticks);
    CHECK(dark->animation_ms == lit->animation_ms);
    size_t size = encode(&dark->game, encoded[0]);
    CHECK(size == encode(&lit->game, encoded[1]) && memcmp(encoded[0], encoded[1], size) == 0);
    CHECK(dark->events.sequence == lit->events.sequence);
    CHECK(memcmp(dark->events.items, lit->events.items, sizeof(dark->events.items)) == 0);
    /* Coos kept sounding on the lit screen; none escaped the dark one. */
    CHECK(lit_cues >= 10u && dark_cues == 0u);
    unsigned asleep_cues = 0u;
    for (uint64_t now = 900500u; now <= 1200000u; now += 500u) {
        CHECK(frame_at(dark, now));
        asleep_cues += jelli_pet_sound_cue(dark, now) ? 1u : 0u;
    }
    CHECK(asleep_cues == 0u);
    dark->ui.sound_pending = true; /* A pending tap cue is consumed silently. */
    CHECK(jelli_pet_sound_cue(dark, 1200000u) == 0u && !dark->ui.sound_pending);
    dark->paused = true; /* Paused hosts consume nothing, as before. */
    dark->ui.sound_pending = true;
    CHECK(jelli_pet_sound_cue(dark, 1200000u) == 0u && dark->ui.sound_pending);
    CHECK(jelli_pet_sound_cue(NULL, 0u) == 0u);
}

/* Open the CARE ring, where a tap at (110, 111) feeds the pet, then let it sleep. */
static JelliPetEngine *care_page_asleep(void)
{
    JelliPetEngine *engine = start_engine(0u, 0u);
    engine->ui.menu_open = true;
    engine->ui.page = JELLI_UI_CARE;
    CHECK(frame_at(engine, 1000u) && frame_at(engine, 179999u) && jelli_pet_screen_awake(engine));
    CHECK(frame_at(engine, 180000u) && !jelli_pet_screen_awake(engine));
    bool awake = true; /* The host acts on the transition, as a real host does each frame. */
    CHECK(jelli_pet_screen_changed(engine, &awake) && !awake);
    return engine;
}

static void expect_undelivered(const JelliPetEngine *engine, uint32_t events, unsigned presents)
{
    CHECK(engine->game.pets[engine->game.active].activity == JELLI_IDLE);
    CHECK(engine->events.sequence == events && fakes[0].presents == presents);
    CHECK(engine->ui.menu_open && engine->ui.page == JELLI_UI_CARE && !engine->paused);
}

static void asleep_inputs_never_reach_ui(void)
{
    JelliPetEngine *engine = care_page_asleep();
    Fake *fake = &fakes[0];
    uint32_t events = engine->events.sequence;
    unsigned presents = fake->presents;
    push(fake, JELLI_TAP, 110, 111);
    CHECK(frame_at(engine, 190000u));
    push(fake, JELLI_SWIPE, 0, 80); /* Would close the ring; also cancels the candidate. */
    CHECK(frame_at(engine, 190100u));
    push(fake, JELLI_TOGGLE_PAUSE, 0, 0);
    push(fake, JELLI_TAP, 110, 111);
    CHECK(frame_at(engine, 190200u) && !jelli_pet_screen_awake(engine));
    push(fake, JELLI_TAP, 110, 111); /* 501 ms after the previous tap: too late. */
    CHECK(frame_at(engine, 190701u) && !jelli_pet_screen_awake(engine));
    push(fake, JELLI_TAP, 300, 300); /* In time but too far away; becomes the candidate. */
    CHECK(frame_at(engine, 190800u) && !jelli_pet_screen_awake(engine));
    expect_undelivered(engine, events, presents);
    push(fake, JELLI_TAP, 330, 290); /* Third tap pairs with the second. */
    CHECK(frame_at(engine, 191000u) && jelli_pet_screen_awake(engine));
    CHECK(fake->presents == presents + 1u && full_damage(fake->damage));
    expect_undelivered(engine, events, presents + 1u);
    bool awake = false;
    CHECK(jelli_pet_screen_changed(engine, &awake) && awake);
    push(fake, JELLI_TAP, 110, 111); /* The extra tap of a triple tap is swallowed. */
    CHECK(frame_at(engine, 191500u));
    CHECK(engine->game.pets[engine->game.active].activity == JELLI_IDLE);
    push(fake, JELLI_TAP, 110, 111); /* After the window, taps work normally. */
    CHECK(frame_at(engine, 191501u));
    CHECK(engine->game.pets[engine->game.active].activity == JELLI_EATING);
}

/* Both taps in one host batch; the waking frame repaints exactly what a fresh render shows. */
static void wake_redraws_like_fresh_render(void)
{
    JelliPetEngine *engine = care_page_asleep();
    Fake *fake = &fakes[0];
    unsigned presents = fake->presents;
    uint32_t events = engine->events.sequence;
    push(fake, JELLI_TAP, 110, 111);
    push(fake, JELLI_TAP, 120, 121);
    CHECK(frame_at(engine, 200000u) && jelli_pet_screen_awake(engine));
    CHECK(fake->presents == presents + 1u && full_damage(fake->damage));
    expect_undelivered(engine, events, presents + 1u);
    memcpy(copy, pixels[0], sizeof(copy));
    engine->ui.rendered = false;
    for (size_t i = 0u; i < sizeof(copy) / sizeof(copy[0]); ++i)
        pixels[0][i] = 0x5555u;
    jelli_pet_render(&engine->surface, &engine->game, &engine->ui, engine->animation_ms,
                     engine->paused);
    CHECK(full_damage(engine->surface.damage) && memcmp(copy, pixels[0], sizeof(copy)) == 0);
    /* The idle timer restarted at the wake. */
    CHECK(frame_at(engine, 379999u) && jelli_pet_screen_awake(engine));
    CHECK(frame_at(engine, 380000u) && !jelli_pet_screen_awake(engine));
}

static void engine_clock_edges(void)
{
    JelliPetEngine *engine = start_engine(0u, 5000u); /* A clock that never advances. */
    for (unsigned i = 0u; i < 1000u; ++i)
        CHECK(frame_at(engine, 5000u));
    CHECK(jelli_pet_screen_awake(engine) && fakes[0].presents == 1001u);
    engine = start_engine(0u, 1000000u); /* A regressing clock restarts the timer. */
    CHECK(frame_at(engine, 500u) && frame_at(engine, 180499u) && jelli_pet_screen_awake(engine));
    CHECK(frame_at(engine, 180500u) && !jelli_pet_screen_awake(engine));
    engine = start_engine(0u, 0u); /* A host resume after ten days. */
    unsigned presents = fakes[0].presents;
    CHECK(frame_at(engine, UINT64_C(864000000)) && !jelli_pet_screen_awake(engine));
    CHECK(fakes[0].presents == presents && jelli_game_valid(&engine->game));
    const uint64_t base = UINT64_MAX - 200000u; /* Near the top of the 64-bit range. */
    engine = start_engine(0u, base);
    CHECK(frame_at(engine, base + 179999u) && jelli_pet_screen_awake(engine));
    CHECK(frame_at(engine, base + 180000u) && !jelli_pet_screen_awake(engine));
    push(&fakes[0], JELLI_TAP, 50, 50);
    CHECK(frame_at(engine, UINT64_MAX - 500u));
    push(&fakes[0], JELLI_TAP, 60, 40);
    presents = fakes[0].presents;
    CHECK(frame_at(engine, UINT64_MAX) && jelli_pet_screen_awake(engine));
    CHECK(fakes[0].presents == presents + 1u && full_damage(fakes[0].damage));
    CHECK(jelli_pet_screen_remaining(&engine->screen, UINT64_MAX) == JELLI_SCREEN_SLEEP_MS);
}

int main(void)
{
    idle_sleep_and_edge();
    every_input_kind_is_activity();
    game_runs_identically_while_asleep();
    asleep_inputs_never_reach_ui();
    wake_redraws_like_fresh_render();
    engine_clock_edges();
    printf("Screen sleep engine checks passed; screen state=%zu bytes\n", sizeof(JelliPetScreen));
    return 0;
}
