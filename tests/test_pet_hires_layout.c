/* Measured layout margins of every creature frame the actor can show (four forms x
 * eight poses, shipped high-resolution art) in each placement the renderer uses.
 * Visible means any sampled alpha above zero, soft edges included. Home: below the
 * "@ HOME" heading (ends at y = 82), above the stat tile (clip starts at y = 282) and
 * right of the battery indicator (rightmost column x = 158 on rows 96..157). Activity
 * pages: below the one-line heading (ends at y = 95) and above the clicker dots
 * (y = 361). Ring pages: the gap to the six ring buttons is reported, not required,
 * because the buttons are drawn over the creature. Prints every minimum and the frame
 * that sets it, so art changes show their effect on the layout. */
#include "jelli/pet_ui.h"
#include "../core/pet_battery.h" /* Internal battery rectangle. */
#include "../core/pet_draw.h"    /* Internal actor layout. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { HEADING_BOTTOM = 82, TILE_TOP = 282, ACTIVITY_HEADING_BOTTOM = 95, DOTS_TOP = 361 };

typedef struct {
    int left, top, right, bottom; /* Inclusive visible extent on screen. */
    int battery_gap;              /* Leftmost visible x on indicator rows minus its edge. */
    long ring_gap_sq;             /* Squared distance to the nearest ring button centre. */
} Extent;

typedef struct {
    int value;
    uint32_t id;
} Minimum;

static void lower(Minimum *m, int value, uint32_t id)
{
    if (value < m->value) {
        m->value = value;
        m->id = id;
    }
}

/* View for pose state 0..7: idle a/b, curious, content, eating, happy, asleep, unwell. */
static JelliPetRenderKey pose_view(uint8_t form, unsigned state, JelliPetPage page, bool menu)
{
    JelliPetRenderKey view = {.form = form, .page = (uint8_t)page, .menu_open = menu};
    if (state < 4u)
        view.phase = (uint8_t)state;
    else if (state == 4u)
        view.activity = JELLI_EATING;
    else if (state == 5u)
        view.activity = JELLI_PLAYING;
    else if (state == 6u)
        view.asleep = true;
    else
        view.health = JELLI_UNWELL;
    return view;
}

static Extent measure(const JelliPetUi *ui)
{
    static const int centers[6][2] = {{110, 111}, {356, 111}, {405, 233},
                                      {356, 355}, {110, 355}, {61, 233}};
    const JelliRect battery = jelli_pet_battery_bounds();
    const int edge = (int)(battery.x + battery.width) - 1;
    Extent e = {INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MAX, LONG_MAX};
    JelliAssetSampler sampler;
    CHECK(jelli_asset_sampler(&sampler, ui->actor_frame, 6u));
    for (unsigned y = 0u; y < sampler.height; ++y)
        for (unsigned x = 0u; x < sampler.width; ++x) {
            uint16_t color;
            if (!jelli_asset_sample(&sampler, x, y, &color))
                continue;
            int sx = ui->actor_x + (int)x, sy = ui->actor_y + (int)y;
            e.left = sx < e.left ? sx : e.left;
            e.top = sy < e.top ? sy : e.top;
            e.right = sx > e.right ? sx : e.right;
            e.bottom = sy > e.bottom ? sy : e.bottom;
            if (sy >= (int)battery.y && sy < (int)(battery.y + battery.height) &&
                sx - edge < e.battery_gap)
                e.battery_gap = sx - edge;
            for (unsigned b = 0u; b < 6u; ++b) {
                long dx = sx - centers[b][0], dy = sy - centers[b][1];
                e.ring_gap_sq =
                    dx * dx + dy * dy < e.ring_gap_sq ? dx * dx + dy * dy : e.ring_gap_sq;
            }
        }
    CHECK(e.right >= e.left); /* A real, visible frame. */
    return e;
}

static Extent place(uint8_t form, unsigned state, JelliPetPage page, bool menu, uint32_t *id)
{
    JelliPetUi ui;
    jelli_pet_ui_init(&ui);
    JelliPetRenderKey view = pose_view(form, state, page, menu);
    jelli_pet_actor_layout(&ui, &view);
    CHECK(ui.actor_frame != NULL && jelli_asset_density(ui.actor_frame) == 6u);
    *id = ui.actor_frame->id;
    return measure(&ui);
}

int main(void)
{
    Minimum heading = {INT_MAX, 0u}, tile = {INT_MAX, 0u}, battery = {INT_MAX, 0u};
    Minimum activity_top = {INT_MAX, 0u}, dots = {INT_MAX, 0u}, ring = {INT_MAX, 0u};
    Minimum panel = {INT_MAX, 0u};
    unsigned frames = 0u;
    for (uint8_t form = 0u; form < JELLI_FORM_COUNT; ++form)
        for (unsigned state = 0u; state < 8u; ++state, ++frames) {
            uint32_t id;
            Extent home = place(form, state, JELLI_UI_HOME, false, &id);
            lower(&heading, home.top - HEADING_BOTTOM - 1, id);
            lower(&tile, TILE_TOP - 1 - home.bottom, id);
            lower(&battery, home.battery_gap, id);
            int side =
                home.left < JELLI_WIDTH - 1 - home.right ? home.left : JELLI_WIDTH - 1 - home.right;
            lower(&panel, side, id);
            Extent busy = place(form, state, JELLI_UI_BRUSH, true, &id);
            lower(&activity_top, busy.top - ACTIVITY_HEADING_BOTTOM - 1, id);
            lower(&dots, DOTS_TOP - 1 - busy.bottom, id);
            Extent ringed = place(form, state, JELLI_UI_CARE, true, &id);
            int gap = 0;
            while ((long)(gap + 49) * (gap + 49) <= ringed.ring_gap_sq)
                ++gap; /* Whole pixels between the creature and a 48 px button disk. */
            lower(&ring, ringed.ring_gap_sq > 48L * 48L ? gap : -1, id);
        }
    printf("%u frames. Home margins: heading %d px (%u), stat tile %d px (%u), battery "
           "indicator %d px (%u), panel sides %d px (%u)\n",
           frames, heading.value, (unsigned)heading.id, tile.value, (unsigned)tile.id,
           battery.value, (unsigned)battery.id, panel.value, (unsigned)panel.id);
    printf("Activity margins: heading %d px (%u), clicker dots %d px (%u). Ring: nearest "
           "button %d px (%u; -1 = overlapped, drawn over the creature)\n",
           activity_top.value, (unsigned)activity_top.id, dots.value, (unsigned)dots.id, ring.value,
           (unsigned)ring.id);
    CHECK(frames == 32u);
    CHECK(heading.value >= 0 && tile.value >= 0 && battery.value >= 1 && panel.value >= 0);
    CHECK(activity_top.value >= 0 && dots.value >= 0);
    puts("PASS: every creature frame fits the home and activity layouts");
    return 0;
}
