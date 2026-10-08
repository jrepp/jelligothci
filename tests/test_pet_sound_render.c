/* Settings SOUND rendering: the level badge, the editor's number and ten-segment bar,
 * dimmed limits, ring transitions, zero damage for unchanged state, and partial
 * redraws that equal a full redraw. Every level's badge and panel is distinct and
 * changes only its own area; nothing changes off the round panel or in the stride
 * padding; the home battery indicator stays hidden while the menu is open. */
#include "jelli/pet_ui.h"
#include "../core/pet_battery.h" /* Internal indicator rectangle under test. */
#include "../core/pet_canvas.h"  /* Internal round-panel mask under test. */

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
/* BG, TEAL and MINT come from core/pet_canvas.h; the edge and geometry from core/pet_sound.c. */
enum { EDGE = 0x738e };
enum { SOUND_X = 61, SOUND_Y = 233, BAR_LEFT = 180, BAR_STEP = 11, BAR_TOP = 244 };
static uint16_t pixels[PIXELS], previous[PIXELS], fresh[PIXELS], saved[PIXELS];
static uint16_t first[PIXELS];

static JelliRect render(const JelliGame *game, JelliPetUi *ui, uint64_t time, bool paused)
{
    memcpy(previous, pixels, sizeof(pixels));
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, ui, time, paused);
    return surface.damage;
}

static bool empty(JelliRect rect) { return rect.width == 0u || rect.height == 0u; }

static uint16_t at(const uint16_t *buffer, int x, int y)
{
    return buffer[(unsigned)y * STRIDE + (unsigned)x];
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

static bool area_differs(const uint16_t *a, const uint16_t *b, int left, int top, int right,
                         int bottom)
{
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (at(a, x, y) != at(b, x, y))
                return true;
    return false;
}

/* The badge under the SOUND button (the same rectangle as the ON/OFF badges). */
static bool badge_differs(const uint16_t *a, const uint16_t *b)
{
    return area_differs(a, b, SOUND_X - 28, SOUND_Y + 20, SOUND_X + 28, SOUND_Y + 44);
}

/* Renders and settles a Settings scene; returns the settled time. */
static uint64_t settle(const JelliGame *game, JelliPetUi *ui, uint64_t time)
{
    (void)render(game, ui, time, false);
    (void)render(game, ui, time + 1000u, false);
    CHECK(!ui->last_view.ring_moving);
    CHECK(empty(render(game, ui, time + 1000u, false))); /* Unchanged: zero damage. */
    check_matches_full_redraw(game, ui, time + 1000u);
    return time + 1000u;
}

static void setup(JelliGame *game, JelliPetUi *ui)
{
    jelli_game_init(game);
    jelli_pet_ui_init(ui);
    ui->menu_open = true;
    ui->page = JELLI_UI_SETTINGS;
    memset(pixels, 0x5a, sizeof(pixels));
}

static void level_badge(void)
{
    JelliGame game;
    JelliPetUi ui;
    setup(&game, &ui);
    uint64_t time = settle(&game, &ui, 0u);
    CHECK(ui.last_view.sound_level == 5u && !ui.last_view.sound_edit);
    CHECK(at(pixels, SOUND_X - 27, SOUND_Y + 21) == TEAL); /* "5" on the ON color. */
    memcpy(saved, pixels, sizeof(pixels));
    game.sound_level = 6u;
    CHECK(render(&game, &ui, time, false).width == JELLI_WIDTH);
    check_matches_full_redraw(&game, &ui, time);
    CHECK(badge_differs(pixels, saved) && ui.last_view.sound_level == 6u);
    CHECK(!area_differs(pixels, saved, 0, 0, JELLI_WIDTH, SOUND_Y + 20)); /* Only the badge. */
    CHECK(empty(render(&game, &ui, time, false)));
    game.sound_level = 0u; /* OFF in words, and the OFF background, not color alone. */
    (void)render(&game, &ui, time, false);
    check_matches_full_redraw(&game, &ui, time);
    CHECK(at(pixels, SOUND_X - 27, SOUND_Y + 21) == BG && badge_differs(pixels, saved));
    game.sound_level = 10u;
    (void)render(&game, &ui, time, false);
    CHECK(at(pixels, SOUND_X - 27, SOUND_Y + 21) == TEAL);
    game.sound_level = 5u;
    (void)render(&game, &ui, time, false);
    CHECK(!area_differs(pixels, saved, 0, 0, JELLI_WIDTH, JELLI_HEIGHT)); /* Back to "5". */
}

/* Filled segments are solid MINT; empty ones are hollow EDGE outlines. */
static void check_bar(unsigned level)
{
    for (unsigned i = 0u; i < JELLI_SOUND_LEVEL_MAX; ++i) {
        int x = BAR_LEFT + (int)i * BAR_STEP;
        bool filled = i < level;
        CHECK(at(pixels, x + 4, BAR_TOP + 7) == (filled ? MINT : BG));
        CHECK(at(pixels, x, BAR_TOP) == (filled ? MINT : EDGE));
        CHECK(at(pixels, x + 7, BAR_TOP + 13) == (filled ? MINT : EDGE));
    }
}

static void editor_screen(bool asleep)
{
    JelliGame game;
    JelliPetUi ui;
    setup(&game, &ui);
    if (asleep) /* Night scene: the editor still works and draws the same panel. */
        CHECK(jelli_game_command(&game, (JelliCommand){JELLI_CMD_REST, game.pets[0].id, 0u}) ==
              JELLI_OK);
    uint64_t time = settle(&game, &ui, 0u);
    memcpy(saved, pixels, sizeof(pixels));
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    CHECK(ui.sound_edit);
    CHECK(render(&game, &ui, time, false).width == JELLI_WIDTH);
    CHECK(ui.last_view.ring_moving && !ui.last_view.ring_sound_edit); /* Old ring leaves. */
    /* The leaving Settings ring starts in place and is not dimmed by the editor's
     * unavailable slots: SOUND (6) and BATTERY (5) keep their full-brightness edges. */
    CHECK(ui.last_view.ring_visible == 255u && (ui.last_view.unavailable & 0x60u) == 0x60u);
    CHECK(at(pixels, SOUND_X, SOUND_Y - 47) == EDGE && at(pixels, 110, 355 - 47) == EDGE);
    time = settle(&game, &ui, time);
    CHECK(ui.last_view.ring_sound_edit && ui.last_view.sound_edit);
    CHECK(area_differs(pixels, saved, 159, 141, 307, 289)); /* The clock panel is replaced. */
    check_bar(5u);
    for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX; ++level) {
        memcpy(saved, pixels, sizeof(pixels));
        unsigned before = game.sound_level;
        game.sound_level = (uint8_t)level;
        JelliRect damage = render(&game, &ui, time, false);
        CHECK(level == before ? empty(damage) : damage.width == JELLI_WIDTH);
        check_matches_full_redraw(&game, &ui, time);
        check_bar(level);
        if (level != before) /* The large number changes. */
            CHECK(area_differs(pixels, saved, 201, 162, 265, 210));
        bool low = level == 0u, high = level == JELLI_SOUND_LEVEL_MAX;
        CHECK(((ui.last_view.unavailable & 2u) != 0u) == low);
        CHECK(((ui.last_view.unavailable & 4u) != 0u) == high);
        /* A limit's button is dimmed: its ring edge is half brightness. */
        uint16_t dim = (uint16_t)((EDGE >> 1) & 0x7bef);
        CHECK(at(pixels, SOUND_X, SOUND_Y - 47) == (low ? dim : EDGE));
        CHECK(at(pixels, 405, SOUND_Y - 47) == (high ? dim : EDGE));
        CHECK(empty(render(&game, &ui, time, false)));
    }
    /* Leaving animates back to the Settings ring with its badge. */
    jelli_pet_ui_back(&ui);
    CHECK(render(&game, &ui, time, false).width == JELLI_WIDTH && ui.last_view.ring_moving);
    (void)settle(&game, &ui, time);
    CHECK(!ui.last_view.ring_sound_edit && !ui.last_view.sound_edit);
    CHECK(at(pixels, SOUND_X - 27, SOUND_Y + 21) == TEAL); /* "10". */
}

static void paused_and_home(void)
{
    JelliGame game;
    JelliPetUi ui;
    setup(&game, &ui);
    (void)render(&game, &ui, 0u, true);
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    (void)render(&game, &ui, 0u, true); /* Paused: no stranded transition. */
    CHECK(!ui.last_view.ring_moving && ui.last_view.ring_sound_edit);
    check_bar(5u);
    jelli_pet_ui_back(&ui);
    (void)render(&game, &ui, 0u, true);
    CHECK(!ui.last_view.ring_moving && !ui.last_view.ring_sound_edit);
    /* On the home scene a level change redraws correctly and costs nothing afterwards. */
    ui.menu_open = false;
    ui.page = JELLI_UI_HOME;
    (void)render(&game, &ui, 0u, false);
    (void)render(&game, &ui, 2000u, false);
    game.sound_level = 0u;
    (void)render(&game, &ui, 2000u, false);
    check_matches_full_redraw(&game, &ui, 2000u);
    CHECK(empty(render(&game, &ui, 2000u, false)));
}

/* Off the round panel and in the stride padding, nothing changes after the first frame. */
static void check_off_panel(void)
{
    for (int y = 0; y < (int)JELLI_HEIGHT; ++y)
        for (int x = 0; x < STRIDE; ++x)
            if (x >= (int)JELLI_WIDTH || !jelli_canvas_in_round(x, y))
                CHECK(at(pixels, x, y) == at(first, x, y));
}

static bool in_disk(int x, int y, int cx, int cy, int radius)
{
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
}

/* Every badge level 0..10 redraws like a fresh frame, costs nothing once unchanged,
 * changes only the badge, and no two levels share a badge image. */
static void every_badge_level(void)
{
    JelliGame game;
    JelliPetUi ui;
    setup(&game, &ui);
    uint64_t time = settle(&game, &ui, 0u);
    memcpy(first, pixels, sizeof(pixels));
    memcpy(saved, pixels, sizeof(pixels)); /* Level 5. */
    static uint16_t badges[JELLI_SOUND_LEVEL_MAX + 1u][24][56];
    for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX; ++level) {
        unsigned before = game.sound_level;
        game.sound_level = (uint8_t)level;
        JelliRect damage = render(&game, &ui, time, false);
        CHECK(level == before ? empty(damage) : damage.width == JELLI_WIDTH);
        check_matches_full_redraw(&game, &ui, time);
        CHECK(empty(render(&game, &ui, time, false)));
        CHECK(!area_differs(pixels, saved, 0, 0, JELLI_WIDTH, SOUND_Y + 20) &&
              !area_differs(pixels, saved, 0, SOUND_Y + 44, JELLI_WIDTH, JELLI_HEIGHT) &&
              !area_differs(pixels, saved, 0, SOUND_Y + 20, SOUND_X - 28, SOUND_Y + 44) &&
              !area_differs(pixels, saved, SOUND_X + 28, SOUND_Y + 20, JELLI_WIDTH, SOUND_Y + 44));
        CHECK(badge_differs(pixels, saved) == (level != 5u));
        check_off_panel();
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 56; ++x)
                badges[level][y][x] = at(pixels, SOUND_X - 28 + x, SOUND_Y + 20 + y);
        for (unsigned other = 0u; other < level; ++other)
            CHECK(memcmp(badges[level], badges[other], sizeof(badges[0])) != 0);
    }
}

/* Editor changes stay in the center panel, the VOL buttons (dimmed at a limit), and
 * the caption; each level has its own panel image. */
static bool editor_area(int x, int y)
{
    return in_disk(x, y, 233, 215, 74) || in_disk(x, y, SOUND_X, SOUND_Y, 48) ||
           in_disk(x, y, 405, SOUND_Y, 48) || (y >= 296 && y < 336);
}

static void editor_changes_stay_in_panel(void)
{
    JelliGame game;
    JelliPetUi ui;
    setup(&game, &ui);
    uint64_t time = settle(&game, &ui, 0u);
    memcpy(first, pixels, sizeof(pixels));
    jelli_pet_ui_tap(&ui, &game, SOUND_X, SOUND_Y);
    time = settle(&game, &ui, time);
    memcpy(saved, pixels, sizeof(pixels)); /* Level 5 editor. */
    static uint16_t panels[JELLI_SOUND_LEVEL_MAX + 1u][148][148];
    for (unsigned level = 0u; level <= JELLI_SOUND_LEVEL_MAX; ++level) {
        game.sound_level = (uint8_t)level;
        (void)render(&game, &ui, time, false);
        check_matches_full_redraw(&game, &ui, time);
        for (int y = 0; y < (int)JELLI_HEIGHT; ++y)
            for (int x = 0; x < (int)JELLI_WIDTH; ++x)
                if (at(pixels, x, y) != at(saved, x, y))
                    CHECK(editor_area(x, y));
        CHECK(area_differs(pixels, saved, 0, 296, JELLI_WIDTH, 336) == (level != 5u));
        /* "LEVEL n OF 10" for 1..9 differs from level 5 only in the digit's 16-pixel
         * cell (x 225..240); "MUTED" and "LEVEL 10 OF 10" change the caption's width. */
        bool digit_only = !area_differs(pixels, saved, 0, 296, 225, 336) &&
                          !area_differs(pixels, saved, 241, 296, JELLI_WIDTH, 336);
        CHECK(digit_only == (level != 0u && level != JELLI_SOUND_LEVEL_MAX));
        check_off_panel();
        for (int y = 0; y < 148; ++y)
            for (int x = 0; x < 148; ++x)
                panels[level][y][x] = at(pixels, 159 + x, 141 + y);
        for (unsigned other = 0u; other < level; ++other)
            CHECK(memcmp(panels[level], panels[other], sizeof(panels[0])) != 0);
    }
}

static void render_into(uint16_t *buffer, const JelliGame *game, JelliPetUi *ui, uint64_t time)
{
    JelliSurface surface = {buffer, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, ui, time, false);
}

/* The home battery indicator is hidden whenever the menu is open, so neither the
 * SOUND button nor the editor ever meets it. While the ring closes the indicator
 * returns and changes only pixels inside its own rectangle, above the SOUND row. */
static void battery_indicator_clear_of_sound(bool editor)
{
    JelliGame on, off;
    JelliPetUi shown, hidden;
    setup(&on, &shown);
    setup(&off, &hidden);
    /* Two readings with different icons and captions: wherever the indicator is drawn
     * the frames differ, and the BATTERY toggle's own ON badge is the same in both. */
    shown.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 50u};
    hidden.power = (JelliPowerReading){JELLI_POWER_CHARGING, 90u};
    if (editor) {
        jelli_pet_ui_tap(&shown, &on, SOUND_X, SOUND_Y);
        jelli_pet_ui_tap(&hidden, &off, SOUND_X, SOUND_Y);
    }
    const JelliRect battery = jelli_pet_battery_bounds();
    CHECK(battery.y + battery.height <= (unsigned)(SOUND_Y - 48)); /* Above the SOUND row. */
    unsigned closing = 0u, shown_frames = 0u;
    for (uint64_t time = 0u; time <= 3000u; time += 25u) {
        if (time == 1000u || time == 2000u) { /* BACK: editor/Settings, then close. */
            jelli_pet_ui_swipe(&shown, &on, 0, 80);
            jelli_pet_ui_swipe(&hidden, &off, 0, 80);
            if (editor && time == 1000u) {
                jelli_pet_ui_swipe(&shown, &on, 0, 80);
                jelli_pet_ui_swipe(&hidden, &off, 0, 80);
            }
        }
        render_into(pixels, &on, &shown, time);
        render_into(fresh, &off, &hidden, time);
        CHECK(shown.last_view.battery_shown == !shown.menu_open);
        bool differs = false;
        for (int y = 0; y < (int)JELLI_HEIGHT; ++y)
            for (int x = 0; x < (int)JELLI_WIDTH; ++x)
                if (at(pixels, x, y) != at(fresh, x, y)) {
                    CHECK(!shown.menu_open && x >= (int)battery.x && y >= (int)battery.y &&
                          x < (int)(battery.x + battery.width) &&
                          y < (int)(battery.y + battery.height));
                    differs = true;
                }
        closing += !shown.menu_open && shown.last_view.ring_visible ? 1u : 0u;
        shown_frames += differs ? 1u : 0u;
    }
    CHECK(closing >= 3u && shown_frames >= 10u); /* The fade-out overlapped the indicator. */
}

int main(void)
{
    level_badge();
    editor_screen(false);
    editor_screen(true);
    paused_and_home();
    every_badge_level();
    editor_changes_stay_in_panel();
    battery_indicator_clear_of_sound(false);
    battery_indicator_clear_of_sound(true);
    puts("PASS: SOUND badge, editor number and hollow/solid bar, dimmed limits, damage, "
         "per-level areas, off-panel pixels, battery indicator clearance");
    return 0;
}
