#include "jelli/pet_ui.h"
#include "../core/pet_battery.h" /* Internal battery rect under test. */
#include "../core/pet_draw.h"    /* Internal actor layout under test. */

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
static uint16_t pixels[PIXELS], fresh[PIXELS];

enum { ACTOR_SCALE = 6 };

/* Alpha of output pixel (x, y) of `frame` drawn at the actor's 6x scale; any value
 * above zero, soft edges included, is visible creature. */
static uint8_t creature_alpha(const JelliAsset *frame, unsigned x, unsigned y)
{
    JelliAssetSampler sampler;
    uint16_t color;
    CHECK(jelli_asset_sampler(&sampler, frame, ACTOR_SCALE));
    return jelli_asset_sample(&sampler, x, y, &color);
}

/* The indicator never paints over any pet: every form x pose x widest caption. Every
 * visible creature pixel, including soft edges blended over the scene, is identical
 * with and without the indicator. */
static void indicator_clears_every_creature(void)
{
    static const JelliPowerReading readings[] = {
        {JELLI_POWER_NO_BATTERY, 0u},
        {JELLI_POWER_DISCHARGING, 15u},
        {JELLI_POWER_DISCHARGING, 5u},
        {JELLI_POWER_CHARGING, 42u},
        {JELLI_POWER_UNKNOWN, JELLI_POWER_PERCENT_UNKNOWN}};
    for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form)
        for (size_t r = 0u; r < sizeof(readings) / sizeof(readings[0]); ++r)
            for (unsigned pose = 0u; pose < 5u; ++pose) {
                JelliGame game;
                jelli_game_init(&game);
                game.active = 2u;
                JelliPet *pet = &game.pets[2];
                pet->form = form;
                if (pose == 1u || pose == 2u) {
                    pet->activity = pose == 1u ? JELLI_EATING : JELLI_PLAYING;
                    pet->interaction_due = 50u;
                } else if (pose == 3u) {
                    pet->asleep = true;
                    pet->nap_due = 36000u;
                } else if (pose == 4u) {
                    pet->health = JELLI_UNWELL;
                }
                JelliPetUi shown;
                jelli_pet_ui_init(&shown);
                shown.power = readings[r];
                JelliSurface with = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
                jelli_pet_render(&with, &game, &shown, 0u, false);
                const JelliAsset *frame = shown.actor_frame;
                CHECK(frame != NULL);
                game.battery_display = false;
                JelliPetUi hidden;
                jelli_pet_ui_init(&hidden);
                hidden.power = readings[r];
                JelliSurface without = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
                jelli_pet_render(&without, &game, &hidden, 0u, false);
                CHECK(hidden.actor_frame == frame);
                for (unsigned row = 0u; row < frame->height * ACTOR_SCALE; ++row)
                    for (unsigned column = 0u; column < frame->width * ACTOR_SCALE; ++column) {
                        if (!creature_alpha(frame, column, row))
                            continue;
                        int x = shown.actor_x + (int)column, y = shown.actor_y + (int)row;
                        CHECK(x >= 0 && y >= 0 && x < (int)JELLI_WIDTH && y < (int)JELLI_HEIGHT);
                        CHECK(pixels[(size_t)y * STRIDE + (size_t)x] ==
                              fresh[(size_t)y * STRIDE + (size_t)x]);
                    }
            }
}

static bool inside(JelliRect r, int x, int y)
{
    return x >= (int)r.x && y >= (int)r.y && x < (int)(r.x + r.width) && y < (int)(r.y + r.height);
}

static bool overlaps(JelliRect a, JelliRect b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

/* Bounding box of every pixel the indicator actually changes, over all icons and the
 * widest captions. The outermost scrim row and column fade to 16/16 (unchanged), so
 * this is one pixel inside the damage rectangle on the left, right and bottom. */
static JelliRect painted_envelope(void)
{
    static const JelliPowerReading readings[] = {
        {JELLI_POWER_NO_BATTERY, 0u},   {JELLI_POWER_DISCHARGING, 15u},
        {JELLI_POWER_DISCHARGING, 5u},  {JELLI_POWER_DISCHARGING, 100u},
        {JELLI_POWER_DISCHARGING, 50u}, {JELLI_POWER_CHARGING, 42u},
        {JELLI_POWER_CHARGED, 100u},    {JELLI_POWER_UNKNOWN, JELLI_POWER_PERCENT_UNKNOWN}};
    int left = JELLI_WIDTH, top = JELLI_HEIGHT, right = -1, bottom = -1;
    for (size_t r = 0u; r < sizeof(readings) / sizeof(readings[0]); ++r) {
        JelliGame game;
        jelli_game_new(&game);
        /* Plain scene: nothing else near the corner. */
        game.pets[game.active].form = JELLI_FORM_MINT;
        JelliPetUi shown, hidden;
        jelli_pet_ui_init(&shown);
        jelli_pet_ui_init(&hidden);
        shown.power = hidden.power = readings[r];
        JelliSurface with = {pixels, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
        jelli_pet_render(&with, &game, &shown, 0u, false);
        game.battery_display = false;
        JelliSurface without = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
        jelli_pet_render(&without, &game, &hidden, 0u, false);
        for (int y = 0; y < (int)JELLI_HEIGHT; ++y)
            for (int x = 0; x < (int)JELLI_WIDTH; ++x)
                if (pixels[(size_t)y * STRIDE + (size_t)x] !=
                    fresh[(size_t)y * STRIDE + (size_t)x]) {
                    left = x < left ? x : left;
                    top = y < top ? y : top;
                    right = x > right ? x : right;
                    bottom = y > bottom ? y : bottom;
                }
    }
    CHECK(right > left && bottom > top);
    JelliRect envelope = {(unsigned)left, (unsigned)top, (unsigned)(right - left + 1),
                          (unsigned)(bottom - top + 1)};
    const JelliRect battery = jelli_pet_battery_bounds();
    CHECK(envelope.x >= battery.x && envelope.y >= battery.y &&
          envelope.x + envelope.width <= battery.x + battery.width &&
          envelope.y + envelope.height <= battery.y + battery.height);
    /* The widest caption's scrim reaches within one pixel of the rectangle's sides. The
     * tallest icon starts at its highest covered logical row (2 px each at 2x); the
     * rectangle reserves the icon's full 16x16 cell above it. */
    unsigned icon_top = 16u;
    for (unsigned icon = 0u; icon < JELLI_BATTERY_ICON_COUNT; ++icon) {
        const JelliAsset *art = jelli_asset_find(JELLI_BATTERY_ASSET_BASE + icon);
        CHECK(art != NULL);
        icon_top = art->top < icon_top ? art->top : icon_top;
    }
    CHECK(envelope.x == battery.x + 1u &&
          envelope.x + envelope.width == battery.x + battery.width - 1u);
    CHECK(envelope.y == battery.y + 2u * icon_top &&
          envelope.y + envelope.height == battery.y + battery.height - 1u);
    return envelope;
}

/* Every frame the actor can show (all eight poses: both idle beats, curious and
 * content included, which a time-zero render never reaches), for every form, keeps
 * every visible pixel, soft edges included, out of the pixels the indicator can
 * change. The closest, happy Lilac, reaches x = 162 at y = 157: four pixels right of
 * the rectangle's unchanged rightmost column (158). */
static void every_pose_clears_indicator(void)
{
    const JelliRect envelope = painted_envelope();
    for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form) {
        uint32_t seen[8] = {0};
        unsigned distinct = 0u;
        for (unsigned state = 0u; state < 8u; ++state) {
            JelliPetRenderKey view = {.form = form};
            if (state < 4u)
                view.phase = (uint8_t)state; /* IDLE_A, IDLE_B, CURIOUS, CONTENT. */
            else if (state == 4u)
                view.activity = JELLI_EATING;
            else if (state == 5u)
                view.activity = JELLI_PLAYING;
            else if (state == 6u)
                view.asleep = true;
            else
                view.health = JELLI_UNWELL;
            JelliPetUi ui;
            jelli_pet_ui_init(&ui);
            view.assets = ui.assets;
            jelli_pet_actor_layout(&ui, &view);
            const JelliAsset *frame = ui.actor_frame;
            CHECK(frame != NULL);
            bool fresh_frame = true;
            for (unsigned i = 0u; i < distinct; ++i)
                fresh_frame = fresh_frame && seen[i] != frame->id;
            if (fresh_frame)
                seen[distinct++] = frame->id;
            unsigned covered = 0u;
            for (unsigned row = 0u; row < frame->height * ACTOR_SCALE; ++row)
                for (unsigned column = 0u; column < frame->width * ACTOR_SCALE; ++column) {
                    if (!creature_alpha(frame, column, row))
                        continue;
                    covered += jelli_asset_hit(frame, column, row, ACTOR_SCALE) ? 1u : 0u;
                    int x = ui.actor_x + (int)column, y = ui.actor_y + (int)row;
                    CHECK(!inside(envelope, x, y));
                }
            /* A real sprite, not a missing or blank frame: over 20 logical pixels. */
            CHECK(covered > 20u * ACTOR_SCALE * ACTOR_SCALE);
        }
        CHECK(distinct == 8u); /* Each state reached a different pose frame. */
    }
}

/* The rectangle sits on the round panel, below both heading lines, above the status
 * caption scrim, and clear of the stat tile, MENU pill, and the gift/catch badge. */
static void indicator_rect_clears_home_ui(void)
{
    const JelliRect battery = jelli_pet_battery_bounds();
    CHECK(battery.x == 30u && battery.y == 96u && battery.width == 129u && battery.height == 62u);
    int bottom = (int)(battery.y + battery.height) - 1;
    /* The rectangle's empty top-left corner is off the panel, but the caption scrim
     * (x 31..157, rows 118..156) and the 32x30 icon box are wholly on it, so neither is
     * clipped unevenly by the round mask. */
    for (int y = 118; y <= 156; ++y)
        CHECK(jelli_canvas_in_round(31, y) && jelli_canvas_in_round(157, y));
    for (int y = 96; y < 126; ++y)
        CHECK(jelli_canvas_in_round(78, y) && jelli_canvas_in_round(109, y));
    CHECK(battery.y >= 84u); /* "@ GARDEN"/"@ HOME" end at y = 82. */
    CHECK(bottom < 254 - 8); /* Status caption scrim starts at 246. */
    CHECK(!overlaps(battery, (JelliRect){90u, 282u, 286u, 100u})); /* Stat tile. */
    CHECK(!overlaps(battery, (JelliRect){161u, 388u, 144u, 64u})); /* MENU pill. */
    CHECK(!overlaps(battery, (JelliRect){194u, 66u, 79u, 100u}));  /* Prize badge + label. */
}

static void render_home(uint16_t *buffer, const JelliGame *game, JelliPetUi *ui, uint64_t time)
{
    JelliSurface surface = {buffer, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
    jelli_pet_render(&surface, game, ui, time, false);
}

/* Pixel-level: toggling each other piece of the resting home scene (heading and
 * creature per form, every status caption, the catch and gift badges, every stat
 * tile page) never changes a pixel inside the indicator rectangle. */
static void home_ui_never_paints_indicator_rect(void)
{
    const JelliRect battery = jelli_pet_battery_bounds();
    for (unsigned variant = 0u; variant < 15u; ++variant) {
        JelliGame base, game;
        jelli_game_init(&base);
        base.battery_display = false;
        base.active = 2u;
        game = base;
        JelliPetUi before, after;
        jelli_pet_ui_init(&before);
        jelli_pet_ui_init(&after);
        JelliPet *pet = &game.pets[2];
        if (variant < 2u)
            pet->form = (uint8_t)variant; /* MINT / LILAC heading and body vs NIRMAL. */
        else if (variant < 5u)
            pet->reaction = (uint8_t)(variant - 1u); /* THAT IS NICE .. TOO MUCH. */
        else if (variant == 5u)
            pet->health = JELLI_RECOVERING;
        else if (variant == 6u)
            pet->health = JELLI_UNWELL;
        else if (variant == 7u) {
            game.prizes.offered = 9u; /* CATCH badge. */
            game.prizes.offered_pet = pet->id;
        } else if (variant == 8u) {
            game.prizes.owned = game.prizes.discovered = 1u << 5;
            game.prizes.origin_pet[5] = 1u;
            after.latched_prize = 6u; /* GIVE badge. */
        } else if (variant == 14u)
            pet->form = JELLI_FORM_UNICORN; /* The widest heading, UNICORN, and its body. */
        else
            after.stat_offset = (uint8_t)(variant - 9u); /* Each stat tile page. */
        render_home(fresh, &base, &before, 0u);
        render_home(pixels, &game, &after, 0u);
        unsigned changed = 0u;
        for (int y = 0; y < (int)JELLI_HEIGHT; ++y)
            for (int x = 0; x < (int)JELLI_WIDTH; ++x)
                if (pixels[(size_t)y * STRIDE + (size_t)x] !=
                    fresh[(size_t)y * STRIDE + (size_t)x]) {
                    CHECK(!inside(battery, x, y));
                    ++changed;
                }
        CHECK(changed > 0u || variant == 9u); /* Every variant (bar the base page) shows up. */
    }
}

/* Closing the ring slides slot 1 (centred at 110,111) out across the indicator while
 * it reappears: every frame of that transition equals a full repaint, and the
 * settled frame shows the indicator exactly as a fresh render does. */
static void ring_close_over_indicator(void)
{
    JelliGame game;
    jelli_game_new(&game);
    JelliPetUi ui;
    jelli_pet_ui_init(&ui);
    ui.power = (JelliPowerReading){JELLI_POWER_DISCHARGING, 15u};
    render_home(pixels, &game, &ui, 0u);
    ui.menu_open = true;
    render_home(pixels, &game, &ui, 10u);
    render_home(pixels, &game, &ui, 2000u);
    CHECK(!ui.last_view.battery_shown && ui.last_view.ring_visible == 255u);
    jelli_pet_ui_back(&ui);
    CHECK(!ui.menu_open);
    const JelliRect battery = jelli_pet_battery_bounds();
    bool moving = false, crossed = false;
    for (uint64_t t = 2000u; t <= 2600u; t += 15u) {
        render_home(pixels, &game, &ui, t);
        moving = moving || ui.last_view.ring_moving;
        if (ui.last_view.ring_moving && ui.last_view.ring_visible > 0u)
            for (unsigned y = battery.y; y < battery.y + battery.height; ++y)
                for (unsigned x = battery.x; x < battery.x + battery.width; ++x)
                    crossed = crossed || pixels[y * STRIDE + x] == 0x4a49u; /* Button face. */
        CHECK(ui.last_view.battery_shown);
        /* Oracle: repaint the whole frame from the same view (ring mid-flight). */
        JelliSurface whole = {fresh, JELLI_WIDTH, JELLI_HEIGHT, STRIDE, {0}};
        jelli_pet_draw(&whole, &game, &ui, &ui.last_view);
        for (size_t y = 0u; y < JELLI_HEIGHT; ++y)
            CHECK(memcmp(&pixels[y * STRIDE], &fresh[y * STRIDE], JELLI_WIDTH * sizeof(uint16_t)) ==
                  0);
    }
    CHECK(moving && !ui.last_view.ring_moving && ui.last_view.ring_visible == 0u);
    /* The settled frame is a fresh home render: no ring remnants over the indicator. */
    JelliPetUi copy = ui;
    copy.rendered = false;
    render_home(fresh, &game, &copy, 2600u);
    for (size_t y = 0u; y < JELLI_HEIGHT; ++y)
        CHECK(memcmp(&pixels[y * STRIDE], &fresh[y * STRIDE], JELLI_WIDTH * sizeof(uint16_t)) == 0);
    CHECK(crossed); /* Slot 1 really passed over the indicator during the transition. */
}

int main(void)
{
    indicator_clears_every_creature();
    every_pose_clears_indicator();
    indicator_rect_clears_home_ui();
    home_ui_never_paints_indicator_rect();
    ring_close_over_indicator();
    puts("Battery layout checks passed");
    return 0;
}
