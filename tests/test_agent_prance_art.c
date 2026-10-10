/* Loki Agent's unicorn stays on the round panel (RFC-003): its real art, embedded as
 * the firmware embeds it (keys creatures.unicorn- and agent.unicorn-), cropped, scaled
 * and placed as ports/esp32-agent/main/ui_art.c and ui.c do, for every pose of every
 * routine and every float of the home unicorn. The crop, scale and placement are
 * reimplemented here line for line (they are firmware-only); the resampling and the
 * drawing, mirror included, are the production core. Coverage counts every pixel the
 * board draws (alpha > 0), so not even a faint edge is cut off by the panel. */
#include "jelli/agent_prance.h"
#include "jelli/agent_scene.h"
#include "jelli/agent_sprite.h"
#include "jelli/assets.h"
#include <math.h>
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

/* ui_art.h, ui_art.c and ui.c. */
enum { SIDE = 466, MARGIN = 10, POSE_A = 1041, POSE_B = 1042, PRANCE = 13001, FLOAT_PX = 4 };

typedef struct {
    JelliAgentSprite home[2];
    JelliAgentSprite prance[JELLI_PRANCE_FRAMES];
    int x, mirror_x, y;
} UiArt;

/* Per sprite row, the first and one past the last column the board draws (alpha > 0
 * for every pixel, alpha >= 128 for the opaque body); left == right for an empty row. */
typedef struct {
    unsigned left, right;
} Span;

static JelliAgentSprite planes(float width_px, float height_px)
{
    CHECK(width_px >= 1.0f && height_px >= 1.0f && width_px <= (float)SIDE &&
          height_px <= (float)SIDE);
    unsigned width = (unsigned)lrintf(width_px), height = (unsigned)lrintf(height_px);
    JelliAgentSprite sprite = {calloc((size_t)width * height, sizeof(uint16_t)),
                               calloc((size_t)width * height, 1u), width, height};
    CHECK(sprite.pixels && sprite.alpha);
    return sprite;
}

/* ui_art.c prepare_home. */
static void prepare_home(UiArt *ui)
{
    const JelliAsset *art[2] = {jelli_asset_find(POSE_A), jelli_asset_find(POSE_B)};
    CHECK(art[0] && art[1]);
    JelliRect crop = jelli_agent_rect_union(jelli_agent_asset_content(art[0]),
                                            jelli_agent_asset_content(art[1]));
    float reach =
        fmaxf(jelli_agent_asset_reach(art[0], crop), jelli_agent_asset_reach(art[1], crop));
    CHECK(crop.width && reach > 0.0f);
    float scale = ((float)SIDE / 2.0f - (float)MARGIN) / reach;
    for (unsigned i = 0u; i < 2u; ++i) {
        ui->home[i] = planes((float)crop.width * scale, (float)crop.height * scale);
        CHECK(jelli_agent_sprite_resample(&ui->home[i], art[i], crop));
    }
}

/* ui_art.c prepare_prance. */
static void prepare_prance(UiArt *ui)
{
    const JelliAsset *art[JELLI_PRANCE_FRAMES];
    JelliRect crop = {0u, 0u, 0u, 0u};
    for (unsigned i = 0u; i < JELLI_PRANCE_FRAMES; ++i) {
        art[i] = jelli_asset_find(PRANCE + i);
        CHECK(art[i] && art[i]->width == art[0]->width && art[i]->density == art[0]->density);
        crop = jelli_agent_rect_union(crop, jelli_agent_asset_content(art[i]));
    }
    float stored = (float)art[0]->width * (float)art[0]->density;
    float scale = (float)JELLI_AGENT_PRANCE_FRAME_PX / stored;
    CHECK(crop.width);
    for (unsigned i = 0u; i < JELLI_PRANCE_FRAMES; ++i) {
        ui->prance[i] = planes((float)crop.width * scale, (float)crop.height * scale);
        CHECK(jelli_agent_sprite_resample(&ui->prance[i], art[i], crop));
    }
    ui->x = (int)lrintf((float)crop.x * scale);
    ui->y = (int)lrintf((float)crop.y * scale);
    ui->mirror_x = JELLI_AGENT_PRANCE_FRAME_PX - ui->x - (int)ui->prance[0].width;
}

/* ui.c placed(), prancing. */
static const JelliAgentSprite *placed(const UiArt *ui, const JelliPrancePose *pose, int *x, int *y)
{
    int left = SIDE / 2 + pose->x - JELLI_AGENT_PRANCE_FRAME_PX / 2;
    int top = SIDE / 2 + pose->y - JELLI_AGENT_PRANCE_FRAME_PX / 2;
    *x = left + (pose->mirror ? ui->mirror_x : ui->x);
    *y = top + ui->y;
    return &ui->prance[pose->frame];
}

static Span span(const JelliAgentSprite *sprite, unsigned row, unsigned least)
{
    Span s = {0u, 0u};
    bool found = false;
    for (unsigned column = 0u; column < sprite->width; ++column) {
        if (sprite->alpha[(size_t)row * sprite->width + column] < least)
            continue;
        s.left = found ? s.left : column;
        s.right = column + 1u;
        found = true;
    }
    return s;
}

/* How far inside the panel's round edge the sprite at (x, y) keeps every pixel of at
 * least alpha least, in pixels (negative when one is outside): each pixel's far
 * corner from the centre counts, so the whole pixel is inside. */
static double margin(const JelliAgentSprite *sprite, int x, int y, bool mirror, unsigned least)
{
    double far = 0.0, centre = (double)SIDE / 2.0;
    for (unsigned row = 0u; row < sprite->height; ++row) {
        Span s = span(sprite, row, least);
        if (s.left == s.right)
            continue;
        int first = mirror ? (int)(sprite->width - s.right) : (int)s.left;
        int last = mirror ? (int)(sprite->width - 1u - s.left) : (int)s.right - 1;
        double dy = fabs((double)(y + (int)row) + 0.5 - centre) + 0.5;
        /* In one row the farthest pixel from the centre is at one end of the span. */
        const int ends[2] = {first, last};
        for (unsigned end = 0u; end < 2u; ++end) {
            double dx = fabs((double)(x + ends[end]) + 0.5 - centre) + 0.5;
            far = fmax(far, dx * dx + dy * dy);
        }
    }
    return centre - sqrt(far);
}

/* The production draw of a sprite whose every covered pixel is forced white puts
 * exactly the spans margin() measures where margin() puts them, mirror included. */
static void the_draw_covers_what_is_measured(uint16_t *pixels, const JelliAgentSprite *sprite,
                                             int x, int y, bool mirror)
{
    static uint16_t white[SIDE * SIDE];
    static uint8_t alpha[SIDE * SIDE];
    CHECK((size_t)sprite->width * sprite->height <= sizeof(alpha));
    for (size_t i = 0u; i < (size_t)sprite->width * sprite->height; ++i) {
        alpha[i] = sprite->alpha[i] ? 255u : 0u;
        white[i] = 0xFFFFu;
    }
    JelliAgentSprite solid = {white, alpha, sprite->width, sprite->height};
    JelliSurface surface = {pixels, SIDE, SIDE, SIDE, {0u, 0u, 0u, 0u}};
    jelli_agent_sprite_draw(&surface, &solid, x, y, mirror, (JelliRect){0u, 0u, SIDE, SIDE});
    size_t drawn = 0u, covered = 0u;
    for (size_t i = 0u; i < (size_t)sprite->width * sprite->height; ++i)
        covered += alpha[i] != 0u;
    for (int row = 0; row < SIDE; ++row) {
        const uint16_t *line = pixels + (size_t)row * SIDE;
        int first = -1, last = -1;
        for (int column = 0; column < SIDE; ++column) {
            if (line[column] != 0xFFFFu)
                continue;
            first = first < 0 ? column : first;
            last = column;
            ++drawn;
        }
        int inside = row - y;
        Span s = inside >= 0 && inside < (int)sprite->height ? span(sprite, (unsigned)inside, 1u)
                                                             : (Span){0u, 0u};
        if (s.left == s.right) {
            CHECK(first < 0);
            continue;
        }
        CHECK(first == x + (mirror ? (int)(sprite->width - s.right) : (int)s.left));
        CHECK(last == x + (mirror ? (int)(sprite->width - 1u - s.left) : (int)s.right - 1));
    }
    CHECK(drawn == covered); /* Nothing clipped by the surface's edges. */
}

typedef struct {
    double all, opaque;
    unsigned poses;
} Worst;

static void measure(Worst *worst, const UiArt *ui, const JelliPrancePose *pose)
{
    int x = 0, y = 0;
    const JelliAgentSprite *sprite = placed(ui, pose, &x, &y);
    worst->all = fmin(worst->all, margin(sprite, x, y, pose->mirror, 1u));
    worst->opaque = fmin(worst->opaque, margin(sprite, x, y, pose->mirror, 128u));
    ++worst->poses;
}

/* Every frame lies within its PRANCE_FRAME_PX frame however it faces, the draw
 * matches the measured coverage for each frame both ways, and facing left is exactly
 * facing right reflected about the frame's centre column (so mirror_x is right). */
static void the_prancing_frames_fit_their_frame(const UiArt *ui)
{
    static uint16_t right[SIDE * SIDE], left[SIDE * SIDE];
    CHECK(ui->x >= 0 && ui->y >= 0 && ui->mirror_x >= 0);
    for (unsigned f = 0u; f < JELLI_PRANCE_FRAMES; ++f) {
        CHECK(ui->prance[f].width == ui->prance[0].width);
        CHECK(ui->prance[f].height == ui->prance[0].height);
        CHECK(ui->x + (int)ui->prance[f].width <= JELLI_AGENT_PRANCE_FRAME_PX);
        CHECK(ui->y + (int)ui->prance[f].height <= JELLI_AGENT_PRANCE_FRAME_PX);
        for (unsigned m = 0u; m < 2u; ++m) {
            JelliPrancePose pose = {(JelliPranceFrame)f, 0, 0, m != 0u};
            int x = 0, y = 0;
            const JelliAgentSprite *sprite = placed(ui, &pose, &x, &y);
            the_draw_covers_what_is_measured(m ? left : right, sprite, x, y, pose.mirror);
        }
        /* The frame is centred on the panel at x = 0, so its centre column is SIDE / 2. */
        for (size_t row = 0u; row < SIDE; ++row) {
            for (size_t column = 0u; column < SIDE; ++column)
                CHECK(right[row * SIDE + column] == left[row * SIDE + SIDE - 1u - column]);
        }
    }
}

/* Every pose of every routine, every millisecond (each distinct pose once). */
static void every_pose_stays_on_the_panel(const UiArt *ui)
{
    static const JelliPranceRoutine routines[] = {JELLI_PRANCE_WANDER, JELLI_PRANCE_CELEBRATE,
                                                  JELLI_PRANCE_FROLIC};
    Worst worst = {1e9, 1e9, 0u};
    for (unsigned r = 0u; r < sizeof(routines) / sizeof(routines[0]); ++r) {
        JelliPrancePose pose, last = {JELLI_PRANCE_FRAMES, 0, 0, false};
        for (uint32_t ms = 0u; ms <= jelli_agent_prance_length(routines[r]); ++ms) {
            (void)jelli_agent_prance_at(routines[r], ms, &pose);
            if (pose.frame != last.frame || pose.x != last.x || pose.y != last.y ||
                pose.mirror != last.mirror)
                measure(&worst, ui, &pose);
            last = pose;
        }
    }
    printf("Prancing: %u poses; nearest the edge %.1f px (any coverage), %.1f px (opaque)\n",
           worst.poses, worst.all, worst.opaque);
    CHECK(worst.poses > 100u);
    CHECK(worst.all >= 0.0 && worst.opaque >= worst.all);
}

/* The home unicorn at its full size, both poses, through its whole float. */
static void the_home_unicorn_stays_on_the_panel(const UiArt *ui)
{
    double all = 1e9, opaque = 1e9;
    for (unsigned p = 0u; p < 2u; ++p) {
        const JelliAgentSprite *sprite = &ui->home[p];
        for (int home_y = -FLOAT_PX; home_y <= FLOAT_PX; ++home_y) {
            int x = SIDE / 2 - (int)sprite->width / 2;
            int y = SIDE / 2 - (int)sprite->height / 2 + home_y;
            all = fmin(all, margin(sprite, x, y, false, 1u));
            opaque = fmin(opaque, margin(sprite, x, y, false, 128u));
        }
        static uint16_t pixels[SIDE * SIDE];
        the_draw_covers_what_is_measured(pixels, sprite, SIDE / 2 - (int)sprite->width / 2,
                                         SIDE / 2 - (int)sprite->height / 2, false);
    }
    printf("Home: %ux%u px; nearest the edge %.1f px (any coverage), %.1f px (opaque)\n",
           ui->home[0].width, ui->home[0].height, all, opaque);
    CHECK(all >= 0.0);
    /* Sized so the opaque body, floated to either side, keeps most of MARGIN. */
    CHECK(opaque >= (double)(MARGIN - FLOAT_PX - 2));
}

/* Only the unicorn and the font are embedded, as on the board. */
static void the_agent_embed_holds_the_unicorn_only(void)
{
    for (uint32_t id = 0u; id < 8u; ++id) {
        CHECK(jelli_asset_find(1041u + id) != NULL);
        CHECK(jelli_asset_find(PRANCE + id) != NULL);
    }
    CHECK(jelli_asset_find(1001u) == NULL && jelli_asset_find(PRANCE + 8u) == NULL);
    CHECK(jelli_asset_glyph((uint8_t)'A') != NULL);
}

int main(void)
{
    static UiArt ui;
    the_agent_embed_holds_the_unicorn_only();
    prepare_home(&ui);
    prepare_prance(&ui);
    the_prancing_frames_fit_their_frame(&ui);
    every_pose_stays_on_the_panel(&ui);
    the_home_unicorn_stays_on_the_panel(&ui);
    for (unsigned i = 0u; i < 2u; ++i) {
        free(ui.home[i].pixels);
        free(ui.home[i].alpha);
    }
    for (unsigned i = 0u; i < JELLI_PRANCE_FRAMES; ++i) {
        free(ui.prance[i].pixels);
        free(ui.prance[i].alpha);
    }
    puts("Agent prance art checks passed");
    return 0;
}
