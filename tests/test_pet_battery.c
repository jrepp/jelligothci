#include "jelli/pet_ui.h"
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

enum { STRIDE = JELLI_WIDTH + 3, PIXELS = STRIDE * JELLI_HEIGHT };
static uint16_t pixels[PIXELS], previous[PIXELS], fresh[PIXELS];
/* The indicator's fixed damage rectangle (core/pet_battery.c). */
static const JelliRect indicator = {308u, 96u, 129u, 62u};

static JelliRect render(const JelliGame *game, JelliPetUi *ui, uint64_t time)
{
    memcpy(previous, pixels, sizeof(pixels));
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, ui, time, false);
    return surface.damage;
}

static bool same_rect(JelliRect a, JelliRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static bool empty(JelliRect rect) { return rect.width == 0u || rect.height == 0u; }

static bool changed_inside(JelliRect rect)
{
    for (unsigned y = rect.y; y < rect.y + rect.height; ++y)
        for (unsigned x = rect.x; x < rect.x + rect.width; ++x)
            if (pixels[y * STRIDE + x] != previous[y * STRIDE + x])
                return true;
    return false;
}

/* A partial update must leave exactly the pixels a full redraw would produce. */
static void check_matches_full_redraw(const JelliGame *game, const JelliPetUi *ui, uint64_t time)
{
    JelliPetUi copy = *ui;
    copy.rendered = false;
    JelliSurface surface = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, &copy, time, false);
    for (size_t y = 0u; y < JELLI_HEIGHT; ++y)
        CHECK(memcmp(&pixels[y * STRIDE], &fresh[y * STRIDE], JELLI_WIDTH * sizeof(uint16_t)) == 0);
}

static bool inside(JelliRect rect, unsigned x, unsigned y)
{
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

static bool contains(JelliRect outer, JelliRect inner)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/* Honors the damage contract: nothing outside it (stride padding included) changed. */
static bool unchanged_outside(JelliRect damage)
{
    for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
        for (unsigned x = 0u; x < STRIDE; ++x)
            if (!inside(damage, x, y) && pixels[y * STRIDE + x] != previous[y * STRIDE + x])
                return false;
    return true;
}

static void indicator_damage(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 80u};
    CHECK(render(&game, &ui, 0u).width == JELLI_WIDTH);
    CHECK(ui.last_view.battery_shown && ui.last_view.battery.percent == 80u);
    CHECK(empty(render(&game, &ui, 100u))); /* Unchanged reading: zero damage. */
    ui.power.percent = 79u;
    CHECK(same_rect(render(&game, &ui, 101u), indicator) && changed_inside(indicator));
    check_matches_full_redraw(&game, &ui, 101u);
    ui.power.percent = 12u; /* Bucket change to LOW: bars and caption both change. */
    CHECK(same_rect(render(&game, &ui, 102u), indicator));
    check_matches_full_redraw(&game, &ui, 102u);
    ui.power = (JelliPowerReading){JELLI_POWER_CHARGED, 98u};
    CHECK(same_rect(render(&game, &ui, 103u), indicator));
    ui.power.percent = 99u; /* FULL ignores the percentage: no redraw. */
    CHECK(empty(render(&game, &ui, 104u)));
    ui.power = (JelliPowerReading){JELLI_POWER_NO_BATTERY, 0u};
    CHECK(same_rect(render(&game, &ui, 105u), indicator));
    ui.power.percent = 50u;
    CHECK(empty(render(&game, &ui, 106u)));
    ui.power_override = (JelliPowerReading){JELLI_POWER_CHARGING, 50u};
    ui.power_override_active = true; /* Debug override wins over the host reading. */
    CHECK(same_rect(render(&game, &ui, 107u), indicator));
    CHECK(ui.last_view.battery.state == JELLI_POWER_CHARGING);
    ui.power_override_active = false;
    CHECK(same_rect(render(&game, &ui, 108u), indicator));
    CHECK(ui.last_view.battery.state == JELLI_POWER_NO_BATTERY);
}

static void hidden_indicator_never_redraws(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 60u};
    (void)render(&game, &ui, 0u);
    game.battery_display = false; /* Setting off: the indicator is erased in its rectangle. */
    CHECK(same_rect(render(&game, &ui, 10u), indicator) && changed_inside(indicator));
    CHECK(!ui.last_view.battery_shown && !ui.last_view.battery_display);
    check_matches_full_redraw(&game, &ui, 10u);
    for (unsigned percent = 0u; percent <= 100u; percent += 7u) {
        ui.power =
            (JelliPowerReading){(uint8_t)(percent % JELLI_POWER_STATE_COUNT), (uint8_t)percent};
        CHECK(empty(render(&game, &ui, 11u + percent)));
    }
    game.battery_display = true;
    ui.menu_open = true; /* Rings occupy the corner; readings must not redraw them. */
    (void)render(&game, &ui, 200u);
    (void)render(&game, &ui, 1000u);
    CHECK(!ui.last_view.battery_shown && ui.last_view.battery_display);
    ui.power = (JelliPowerReading){JELLI_POWER_CHARGING, 5u};
    CHECK(empty(render(&game, &ui, 1001u)));
}

/* Every state and threshold, in sequence: a partial redraw is pixel-identical to
 * a full one, stays inside its damage, and unchanged canonical readings are free. */
static void partial_redraw_matches_full(bool asleep)
{
    static const uint8_t levels[] = {80u, 70u, 69u, 25u, 24u, 10u, 9u, 0u, 100u, 255u, 101u};
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    if (asleep) /* Night scene with sleep particles drifting over the panel. */
        CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, game.pets[0].id, 0u}) ==
              JELLI_OK);
    memset(pixels, 0x5a, sizeof(pixels)); /* Sentinel stride padding. */
    (void)render(&game, &ui, 0u);
    unsigned redraws = 0u;
    for (unsigned state = 0u; state <= JELLI_POWER_STATE_COUNT; ++state) {
        for (unsigned i = 0u; i < sizeof(levels); ++i) {
            JelliPowerReading before = ui.last_view.battery;
            ui.power = (JelliPowerReading){(uint8_t)state, levels[i]};
            JelliPowerReading after = jelli_power_normalize(ui.power);
            uint64_t time = asleep ? 50u * (state * sizeof(levels) + i) : 0u;
            JelliRect damage = render(&game, &ui, time);
            CHECK(unchanged_outside(damage));
            check_matches_full_redraw(&game, &ui, time);
            CHECK(ui.last_view.battery.state == after.state &&
                  ui.last_view.battery.percent == after.percent);
            bool same = before.state == after.state && before.percent == after.percent;
            if (!asleep)
                CHECK(same ? empty(damage) : same_rect(damage, indicator));
            else if (!same)
                CHECK(contains(damage, indicator));
            redraws += same ? 0u : 1u;
        }
    }
    CHECK(redraws >= 20u); /* The sequence really exercises many distinct indicators. */
}

/* Every pixel the indicator can draw, for each icon and the widest captions,
 * lies inside the fixed damage rectangle (so a partial redraw can erase it). */
static void indicator_stays_in_bounds(void)
{
    static const JelliPowerReading readings[] = {
        {JELLI_POWER_DISCHARGING, 0u},  {JELLI_POWER_DISCHARGING, 10u},
        {JELLI_POWER_DISCHARGING, 50u}, {JELLI_POWER_DISCHARGING, 100u},
        {JELLI_POWER_CHARGING, 100u},   {JELLI_POWER_NO_BATTERY, 0u},
        {JELLI_POWER_CHARGED, 0u},      {JELLI_POWER_UNKNOWN, 0u}};
    JelliGame shown, hidden;
    JelliPetUi ui;
    jelli_game_init(&shown);
    hidden = shown;
    hidden.battery_display = false;
    jelli_pet_ui_init(&ui);
    JelliSurface surface = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, &hidden, &ui, 0u, false);
    memcpy(previous, fresh, sizeof(fresh));
    for (unsigned r = 0u; r < sizeof(readings) / sizeof(readings[0]); ++r) {
        JelliPetUi copy;
        jelli_pet_ui_init(&copy);
        copy.power = readings[r];
        jelli_pet_render(&surface, &shown, &copy, 0u, false);
        unsigned differing = 0u;
        for (unsigned y = 0u; y < JELLI_HEIGHT; ++y)
            for (unsigned x = 0u; x < JELLI_WIDTH; ++x)
                if (fresh[y * STRIDE + x] != previous[y * STRIDE + x]) {
                    CHECK(inside(indicator, x, y));
                    ++differing;
                }
        CHECK(differing > 100u); /* Icon and caption are actually drawn. */
        /* Legibility scrim: row 151 is below the glyphs yet darkened, never brightened. */
        unsigned shown_sum = 0u, hidden_sum = 0u;
        for (unsigned x = 364u; x <= 380u; ++x) {
            uint16_t on = fresh[151u * STRIDE + x], off = previous[151u * STRIDE + x];
            CHECK((on >> 11) <= (off >> 11) && ((on >> 5) & 63u) <= ((off >> 5) & 63u) &&
                  (on & 31u) <= (off & 31u));
            shown_sum += (on >> 11) + ((on >> 5) & 63u) + (on & 31u);
            hidden_sum += (off >> 11) + ((off >> 5) & 63u) + (off & 31u);
        }
        CHECK(shown_sum < hidden_sum);
    }
}

/* Rings, the collection, and activities cover the corner: readings never redraw. */
static void covered_scenes_ignore_readings(void)
{
    static const JelliPetPage pages[] = {JELLI_UI_HOME,       JELLI_UI_CARE,     JELLI_UI_MORE,
                                         JELLI_UI_COLLECTION, JELLI_UI_SETTINGS, JELLI_UI_MOMENTS,
                                         JELLI_UI_HEALTH,     JELLI_UI_BRUSH,    JELLI_UI_STRETCH};
    for (unsigned p = 0u; p < sizeof(pages) / sizeof(pages[0]); ++p) {
        JelliGame game;
        JelliPetUi ui;
        jelli_game_init(&game);
        jelli_pet_ui_init(&ui);
        ui.menu_open = true;
        ui.page = pages[p];
        ui.clicker_goal = 3u;
        ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 60u};
        (void)render(&game, &ui, 0u);
        (void)render(&game, &ui, 2000u); /* Let the ring transition settle. */
        CHECK(empty(render(&game, &ui, 2000u)));
        CHECK(!ui.last_view.battery_shown && ui.last_view.battery_display);
        ui.power = (JelliPowerReading){JELLI_POWER_CHARGING, 5u};
        CHECK(empty(render(&game, &ui, 2000u)));
        ui.power_override = (JelliPowerReading){JELLI_POWER_NO_BATTERY, 0u};
        ui.power_override_active = true;
        CHECK(empty(render(&game, &ui, 2000u)));
    }
}

/* A tile change alone keeps the tile damage; together with an indicator change
 * the frame falls back to a full redraw rather than dropping either region. */
static void combined_changes_redraw_fully(void)
{
    JelliGame game;
    JelliPetUi ui;
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 80u};
    (void)render(&game, &ui, 0u);
    const JelliRect tile = {90u, 282u, 286u, 100u};
    game.pets[0].needs[0] = (uint16_t)(game.pets[0].needs[0] / 2u);
    CHECK(same_rect(render(&game, &ui, 0u), tile));
    check_matches_full_redraw(&game, &ui, 0u);
    game.pets[0].needs[0] = (uint16_t)(game.pets[0].needs[0] / 2u);
    ui.power.percent = 20u;
    CHECK(render(&game, &ui, 0u).width == JELLI_WIDTH);
    check_matches_full_redraw(&game, &ui, 0u);
    CHECK(ui.last_view.battery.percent == 20u);
}

static void settings_toggle_persists(void)
{
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events = {0};
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.events = &events;
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    JelliPetUiButton button;
    CHECK(jelli_pet_ui_control(&ui, 5u, false, &button) && strcmp(button.label, "BATTERY") == 0);
    CHECK(button.icon == JELLI_BATTERY_ASSET_BASE + JELLI_BATTERY_HIGH && button.scale == 4u);
    CHECK(button.bounds.x == 62u && button.bounds.y == 307u && button.circular);
    CHECK(jelli_pet_ui_available(&ui, &game, 5u) == JELLI_OK);
    jelli_pet_ui_tap(&ui, &game, 110, 355);
    CHECK(!game.battery_display && ui.result == JELLI_OK && ui.sound_pending);
    CHECK(ui.save_requested && ui.save_status == JELLI_SAVE_PENDING);
    CHECK(ui.menu_open && ui.page == JELLI_UI_SETTINGS);
    const JelliEvent *last = jelli_events_at(&events, events.count - 1u);
    CHECK(last && last->kind == JELLI_EVENT_INPUT && last->code == JELLI_UI_INPUT_BATTERY);
    (void)render(&game, &ui, 0u);
    CHECK(!ui.last_view.battery_display);

    uint8_t bytes[JELLI_SAVE_CAPACITY];
    JelliSave save = {.game = game, .sequence = 1u}, loaded;
    size_t size = jelli_save_encode(&save, bytes, sizeof(bytes));
    CHECK(size && jelli_save_decode(&loaded, bytes, size) && !loaded.game.battery_display);

    /* A busy pet does not close Settings for a display-only toggle. */
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_FEED, game.pets[0].id, 0u}) ==
          JELLI_OK);
    ui.save_requested = false;
    jelli_pet_ui_tap(&ui, &game, 110, 355);
    CHECK(game.battery_display && ui.save_requested && ui.menu_open);
    CHECK(ui.page == JELLI_UI_SETTINGS && jelli_game_valid(&game));
    /* Clock editing reuses slot 5 as MIN -, leaving the setting alone. */
    ui.clock_edit = true;
    jelli_pet_ui_tap(&ui, &game, 110, 355);
    CHECK(game.battery_display && ui.clock_adjust == 1439);
}

static bool region_differs(const uint16_t *a, const uint16_t *b, int cx, int cy)
{
    for (int y = cy + 20; y < cy + 44; ++y) /* The ON/OFF badge under a ring button. */
        for (int x = cx - 28; x < cx + 28; ++x)
            if (a[y * STRIDE + x] != b[y * STRIDE + x])
                return true;
    return false;
}

/* The toggle works asleep without waking the pet, keeps every other Settings slot,
 * and its ON/OFF badge (text, not only color) follows the setting. */
static void settings_toggle_asleep(void)
{
    static const char *const awake_labels[] = {"BEDTIME +1H", "SWITCH PET", "REST", NULL,
                                               "BATTERY"};
    JelliGame game;
    JelliPetUi ui;
    JelliEventLog events = {0};
    jelli_game_init(&game);
    jelli_pet_ui_init(&ui);
    game.events = &events;
    ui.menu_open = true;
    ui.page = JELLI_UI_SETTINGS;
    JelliPetUiButton button, rest;
    for (unsigned slot = 1u; slot <= 5u; ++slot)
        if (awake_labels[slot - 1u])
            CHECK(jelli_pet_ui_control(&ui, slot, false, &button) &&
                  strcmp(button.label, awake_labels[slot - 1u]) == 0);
    CHECK(!jelli_pet_ui_control(&ui, 6u, false, &button));
    CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, game.pets[0].id, 0u}) ==
          JELLI_OK);
    CHECK(jelli_pet_ui_control(&ui, 3u, true, &rest) && strcmp(rest.label, "WAKE") == 0);
    CHECK(jelli_pet_ui_control(&ui, 5u, true, &button) && strcmp(button.label, "BATTERY") == 0);
    CHECK(button.icon == 12004u && jelli_pet_ui_available(&ui, &game, 5u) == JELLI_OK);
    (void)render(&game, &ui, 0u);
    (void)render(&game, &ui, 2000u);
    memcpy(fresh, pixels, sizeof(pixels));
    int cx = (int)(button.bounds.x + 48u), cy = (int)(button.bounds.y + 48u);
    jelli_pet_ui_tap(&ui, &game, cx, cy);
    CHECK(!game.battery_display && game.pets[0].asleep && ui.result == JELLI_OK);
    CHECK(ui.sound_pending && ui.save_requested && ui.menu_open && ui.page == JELLI_UI_SETTINGS);
    const JelliEvent *last = jelli_events_at(&events, events.count - 1u);
    CHECK(last && last->code == JELLI_UI_INPUT_BATTERY && last->result == JELLI_OK);
    CHECK(render(&game, &ui, 2000u).width == JELLI_WIDTH);
    CHECK(region_differs(pixels, fresh, cx, cy)); /* ON became OFF. */
    CHECK(!region_differs(pixels, fresh, (int)rest.bounds.x + 48, (int)rest.bounds.y + 48));
    jelli_pet_ui_tap(&ui, &game, cx, cy);
    CHECK(game.battery_display && game.pets[0].asleep);
    (void)render(&game, &ui, 2000u);
    CHECK(!region_differs(pixels, fresh, cx, cy)); /* Back to ON: identical badge. */
    jelli_pet_ui_back(&ui); /* Back still leaves Settings for the home ring, then closes. */
    CHECK(ui.menu_open && ui.page == JELLI_UI_HOME);
    jelli_pet_ui_back(&ui);
    CHECK(!ui.menu_open);
    (void)render(&game, &ui, 4000u);
    (void)render(&game, &ui, 6000u);
    CHECK(ui.last_view.battery_shown);
}

int main(void)
{
    indicator_damage();
    hidden_indicator_never_redraws();
    partial_redraw_matches_full(false);
    partial_redraw_matches_full(true);
    indicator_stays_in_bounds();
    covered_scenes_ignore_readings();
    combined_changes_redraw_fully();
    settings_toggle_persists();
    settings_toggle_asleep();
    puts("PASS: battery indicator damage, hidden/unchanged readings, settings toggle persistence");
    return 0;
}
