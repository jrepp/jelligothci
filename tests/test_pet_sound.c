/* Settings SOUND: the ring button and its editor, limits, persistence, input events,
 * and the volume a fake host requests after each press. Rendering and damage are in
 * tests/test_pet_sound_render.c. */
#include "jelli/pet_engine.h"
#include "jelli/save.h"

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

/* Ring button centers: SOUND (Settings slot 6) and, while editing, VOL - and VOL +. */
enum { SOUND_X = 61, SOUND_Y = 233, PLUS_X = 405, PLUS_Y = 233, CLOCK_X = 233, CLOCK_Y = 267 };

static void open_settings(JelliGame *game, JelliPetUi *ui, JelliEventLog *events)
{
    jelli_game_init(game);
    jelli_pet_ui_init(ui);
    *events = (JelliEventLog){0};
    game->events = events;
    ui->menu_open = true;
    ui->page = JELLI_UI_SETTINGS;
}

static const JelliEvent *last_event(const JelliEventLog *events)
{
    return events->count ? jelli_events_at(events, events->count - 1u) : NULL;
}

static void button_and_slots(void)
{
    static const char *const labels[] = {"BEDTIME +1H", "SWITCH PET", "REST",
                                         "CLOCK",       "BATTERY",    "SOUND"};
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    open_settings(&game, &ui, &events);
    JelliPetUiButton button;
    for (unsigned slot = 1u; slot <= 6u; ++slot) /* Existing slot IDs are unchanged. */
        CHECK(jelli_pet_ui_control(&ui, slot, false, &button) &&
              strcmp(button.label, labels[slot - 1u]) == 0);
    CHECK(jelli_pet_ui_control(&ui, JELLI_UI_SOUND_SLOT, true, &button)); /* Asleep too. */
    CHECK(strcmp(button.label, "SOUND") == 0 && button.icon == 9006u && button.scale == 3u);
    CHECK(button.bounds.x == SOUND_X - 48 && button.bounds.y == SOUND_Y - 48);
    CHECK(button.bounds.width == 96u && button.circular);
    CHECK(jelli_pet_ui_available(&ui, &game, JELLI_UI_SOUND_SLOT) == JELLI_OK);
    CHECK(jelli_pet_ui_item(JELLI_UI_SETTINGS, 5u, false).action == JELLI_UI_ACTION_SOUND);
    ui.page = JELLI_UI_HOME; /* Only Settings has the button. */
    CHECK(!jelli_pet_sound_slot(&ui, JELLI_UI_SOUND_SLOT));
}

static void enter_editor(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    open_settings(&game, &ui, &events);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    CHECK(ui.sound_edit && !ui.clock_edit && ui.result == JELLI_OK && ui.sound_pending);
    CHECK(!ui.save_requested && game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    CHECK(ui.menu_open && ui.page == JELLI_UI_SETTINGS);
    const JelliEvent *event = last_event(&events);
    CHECK(event && event->kind == JELLI_EVENT_INPUT && event->code == JELLI_UI_INPUT_SOUND);
    CHECK(event->input == JELLI_UI_INPUT_SOUND + 1u && event->result == JELLI_OK);
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&ui, 1u, false, &button) && strcmp(button.label, "VOL -") == 0);
    CHECK(button.bounds.x + 48u == SOUND_X && button.bounds.y + 48u == SOUND_Y && !button.icon);
    CHECK(jelli_pet_ui_control(&ui, 2u, false, &button) && strcmp(button.label, "VOL +") == 0);
    CHECK(button.bounds.x + 48u == PLUS_X && button.bounds.y + 48u == PLUS_Y);
    for (unsigned slot = 3u; slot <= 9u; ++slot) /* No CLOCK, BATTERY, or other setting. */
        CHECK(!jelli_pet_ui_control(&ui, slot, false, &button));
    CHECK(jelli_pet_ui_control(&ui, 0u, false, &button) && strcmp(button.label, "BACK") == 0);
    jelli_pet_ui_tap(&ui, &game, CLOCK_X, CLOCK_Y); /* The hidden clock gear is not a target. */
    CHECK(ui.sound_edit && !ui.clock_edit);
}

static void leave_editor(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    open_settings(&game, &ui, &events);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    CHECK(ui.sound_edit);
    jelli_pet_ui_back(&ui); /* BACK leaves the editor first, like the clock editor. */
    CHECK(!ui.sound_edit && ui.menu_open && ui.page == JELLI_UI_SETTINGS);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    CHECK(ui.sound_edit);
    jelli_pet_ui_swipe(&ui, &game, 0, 80); /* A downward swipe is BACK too. */
    CHECK(!ui.sound_edit && ui.page == JELLI_UI_SETTINGS && ui.menu_open);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(ui.page == JELLI_UI_HOME && ui.menu_open);
    jelli_pet_ui_swipe(&ui, &game, 0, 80);
    CHECK(!ui.menu_open && !ui.sound_edit);
}

/* Each step persists and reports 42 + slot; the maximum reports NOT_READY unchanged. */
static void steps_up_to_limit(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    open_settings(&game, &ui, &events);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    for (unsigned expected = 6u; expected <= JELLI_SOUND_LEVEL_MAX; ++expected) {
        ui.save_requested = false;
        ui.save_status = JELLI_SAVE_SAVED;
        ui.sound_pending = false;
        jelli_pet_ui_tap(&ui, &game, PLUS_X, PLUS_Y);
        CHECK(game.sound_level == expected && ui.result == JELLI_OK && ui.sound_pending);
        CHECK(ui.save_requested && ui.save_status == JELLI_SAVE_PENDING);
        const JelliEvent *event = last_event(&events);
        CHECK(event && event->code == JELLI_UI_INPUT_SOUND + 2u && event->result == JELLI_OK);
    }
    ui.save_requested = false;
    ui.sound_pending = false;
    uint32_t sequence = events.sequence;
    CHECK(jelli_pet_ui_available(&ui, &game, 2u) == JELLI_NOT_READY);
    jelli_pet_ui_tap(&ui, &game, PLUS_X, PLUS_Y);
    CHECK(game.sound_level == JELLI_SOUND_LEVEL_MAX && ui.result == JELLI_NOT_READY);
    CHECK(!ui.save_requested && !ui.sound_pending && ui.sound_edit);
    CHECK(events.sequence == sequence + 1u && last_event(&events)->result == JELLI_NOT_READY);
}

/* Down to muted, which is the lower limit; the level persists through the codec. */
static void steps_down_to_mute(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    open_settings(&game, &ui, &events);
    game.sound_level = JELLI_SOUND_LEVEL_MAX;
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    CHECK(ui.sound_edit);
    for (unsigned expected = JELLI_SOUND_LEVEL_MAX; expected-- > 0u;) {
        ui.save_requested = false;
        jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
        CHECK(game.sound_level == expected && ui.result == JELLI_OK && ui.save_requested);
        CHECK(last_event(&events)->code == JELLI_UI_INPUT_SOUND + 1u);
    }
    ui.save_requested = false;
    CHECK(jelli_pet_ui_available(&ui, &game, 1u) == JELLI_NOT_READY);
    CHECK(jelli_pet_ui_available(&ui, &game, 2u) == JELLI_OK);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    CHECK(game.sound_level == 0u && ui.result == JELLI_NOT_READY && !ui.save_requested);
    CHECK(jelli_game_valid(&game));
    /* Direct calls honor the same limits and never step outside 0..10. */
    jelli_pet_sound_action(&ui, &game, 1u);
    CHECK(game.sound_level == 0u && ui.result == JELLI_NOT_READY);
    jelli_pet_sound_action(&ui, &game, 3u);
    CHECK(game.sound_level == 0u && ui.result == JELLI_INVALID_TARGET);
    CHECK(!jelli_pet_sound_button(0u, &(JelliPetUiButton){0}) && !jelli_pet_sound_button(1u, NULL));

    uint8_t bytes[JELLI_SAVE_CAPACITY];
    JelliSave save = {.game = game, .sequence = 1u}, loaded;
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size) && loaded.game.sound_level == 0u);
}

/* The clock editor keeps its slots, and the two editors never open together. */
static void clock_editor_unaffected(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events;
    open_settings(&game, &ui, &events);
    jelli_pet_ui_tap(&ui, &game, CLOCK_X, CLOCK_Y);
    CHECK(ui.clock_edit && !ui.sound_edit && last_event(&events)->code == 33u);
    CHECK(!jelli_pet_sound_slot(&ui, JELLI_UI_SOUND_SLOT));
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&ui, 6u, false, &button) && strcmp(button.label, "MIN +") == 0);
    CHECK(jelli_pet_ui_control(&ui, 3u, false, &button) && strcmp(button.label, "HR -") == 0);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y); /* HR - shares SOUND's position. */
    CHECK(ui.clock_adjust == 1380 && !ui.sound_edit);
    CHECK(game.sound_level == JELLI_SOUND_LEVEL_DEFAULT && last_event(&events)->code == 36u);
    jelli_pet_ui_back(&ui);
    CHECK(!ui.clock_edit && !ui.sound_edit && ui.page == JELLI_UI_SETTINGS);
}

typedef struct {
    uint64_t now;
    JelliInput pending;
    bool ready;
} Fake;

static uint64_t now_ms(void *context) { return ((Fake *)context)->now; }

static bool poll(void *context, JelliInput *input)
{
    Fake *fake = context;
    if (!fake->ready)
        return false;
    *input = fake->pending;
    fake->ready = false;
    return true;
}

static void present(void *context, const JelliSurface *surface)
{
    (void)context;
    CHECK(surface->pixels != NULL);
}

/* One host frame, as both ports run it: the engine frame, then the cue at the
 * current level's volume. Returns the requested volume, zero for no request. */
static unsigned host_frame(JelliPetEngine *engine, const Fake *fake, unsigned *cue)
{
    CHECK(jelli_pet_frame(engine));
    unsigned next =
        jelli_pet_ui_sound(&engine->ui, &engine->game.pets[engine->game.active], fake->now);
    unsigned volume = next ? jelli_sound_volume(engine->game.sound_level, next - 1u) : 0u;
    *cue = next;
    return volume;
}

static unsigned tap_and_hear(JelliPetEngine *engine, Fake *fake, int x, int y, unsigned *cue)
{
    unsigned ignored;
    for (unsigned i = 0u; i < 2u; ++i) { /* Settle any ring transition; pass tap coalescing. */
        fake->now += 400u; /* The ring transition lasts 360 ms at the default scale. */
        (void)host_frame(engine, fake, &ignored);
    }
    CHECK(!engine->ui.last_view.ring_moving);
    fake->pending = (JelliInput){JELLI_TAP, x, y};
    fake->ready = true;
    return host_frame(engine, fake, cue);
}

static void press_is_heard_at_new_level(void)
{
    static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
    static JelliPetEngine engine;
    Fake fake = {0};
    JelliPlatform platform = {&fake, now_ms, poll, present, NULL};
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    CHECK(jelli_pet_init(&engine, platform, surface));
    CHECK(engine.game.sound_level == JELLI_SOUND_LEVEL_DEFAULT);
    unsigned cue;
    CHECK(tap_and_hear(&engine, &fake, 233, 420, &cue) == 25u && cue == 6u); /* MENU at 5. */
    engine.ui.page = JELLI_UI_SETTINGS;
    CHECK(tap_and_hear(&engine, &fake, SOUND_X, SOUND_Y, &cue) == 25u && engine.ui.sound_edit);
    CHECK(tap_and_hear(&engine, &fake, PLUS_X, PLUS_Y, &cue) == 30u && cue == 6u);
    CHECK(engine.game.sound_level == 6u && engine.ui.save_requested);
    for (unsigned level = 5u; level > 0u; --level) {
        unsigned volume = tap_and_hear(&engine, &fake, SOUND_X, SOUND_Y, &cue);
        CHECK(engine.game.sound_level == level && cue == 6u);
        CHECK(volume == jelli_sound_volume(level, JELLI_SOUND_TAP) && volume > 0u);
    }
    /* Down to 0: the tap cue is still chosen, but muted it yields no request. */
    CHECK(tap_and_hear(&engine, &fake, SOUND_X, SOUND_Y, &cue) == 0u && cue == 6u);
    CHECK(engine.game.sound_level == 0u);
    /* At the limit nothing changes and nothing plays. */
    CHECK(tap_and_hear(&engine, &fake, SOUND_X, SOUND_Y, &cue) == 0u && cue == 0u);
    CHECK(engine.ui.result == JELLI_NOT_READY);
    /* Muted, the pet's coos stay silent too once the menu closes. */
    engine.ui.sound_edit = false;
    engine.ui.menu_open = false;
    engine.ui.page = JELLI_UI_HOME;
    unsigned requests = 0u, coos = 0u;
    for (unsigned i = 0u; i < 600u; ++i) {
        fake.now += 100u;
        requests += host_frame(&engine, &fake, &cue) ? 1u : 0u;
        coos += cue == 7u ? 1u : 0u;
    }
    CHECK(requests == 0u && coos > 0u);
    CHECK(tap_and_hear(&engine, &fake, 233, 420, &cue) == 0u); /* MENU, muted. */
    engine.ui.page = JELLI_UI_SETTINGS;
    (void)tap_and_hear(&engine, &fake, SOUND_X, SOUND_Y, &cue);
    CHECK(tap_and_hear(&engine, &fake, PLUS_X, PLUS_Y, &cue) == 5u); /* Level 1 is heard. */
}

int main(void)
{
    button_and_slots();
    enter_editor();
    leave_editor();
    steps_up_to_limit();
    steps_down_to_mute();
    clock_editor_unaffected();
    press_is_heard_at_new_level();
    puts("PASS: Settings SOUND button, level editor limits, persistence, events, host volume");
    return 0;
}
