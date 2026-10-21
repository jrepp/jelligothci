#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli/creature.h"
#include "jelli_asset_ids.h"
#include <stdio.h>
#include <string.h>

static void controls(Canvas *c, const JelliPetUi *ui)
{
    for (unsigned slot = 0; slot <= 6u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collectible_button(ui, slot, &b))
            continue;
        if (!slot) {
            jelli_canvas_disk(c, 233, 462, 48, TEAL);
            jelli_canvas_centered(c, "BACK", 432, 2u, PALE);
            continue;
        }
        jelli_canvas_rect(c, (int)b.bounds.x, (int)b.bounds.y, (int)b.bounds.width,
                          (int)b.bounds.height, TEAL);
        unsigned scale = b.bounds.width >= 200u && strlen(b.label) <= 12u ? 2u : 1u;
        int width = (int)(strlen(b.label) * 8u * scale);
        jelli_canvas_text(c, b.label, (int)(b.bounds.x + b.bounds.width / 2u) - width / 2,
                          (int)b.bounds.y + 16, scale, PALE);
    }
}

static unsigned cheer_beat(const JelliPetUi *ui, const JelliGame *game, bool celebrate)
{
    if (ui->collectible_reduced_motion || game->collectible.paused)
        return 0u;
    if (game->collectible.mode == JELLI_COLLECTIBLE_RECORD)
        return game->collectible.record_cursor / 11025u % 2u;
    if (!celebrate)
        return 0u;
    unsigned ticks = game->collectible.elapsed_ticks;
    if (game->collectible.mode == JELLI_COLLECTIBLE_BUBBLE ||
        game->collectible.mode == JELLI_COLLECTIBLE_CHAIN)
        ticks = game->pets[game->active].reaction_ticks;
    return ticks / 2u % 2u;
}

static void cheer(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    const JelliPet *pet = &game->pets[game->active];
    bool lost = jelli_pet_collectible_lost(&game->collectible);
    bool celebrate = !lost && ((game->collectible.mode != JELLI_COLLECTIBLE_CHAIN &&
                                game->collectible.mode != JELLI_COLLECTIBLE_BUBBLE) ||
                               pet->reaction == JELLI_REACTION_TOUCH_HAPPY);
    const JelliClip *clip = jelli_creature_clip(pet->form, lost        ? JELLI_POSE_UNWELL
                                                           : celebrate ? JELLI_POSE_HAPPY
                                                                       : JELLI_POSE_IDLE);
    if (!clip || !clip->count)
        return;
    unsigned beat = cheer_beat(ui, game, celebrate);
    unsigned frame = beat % clip->count;
    jelli_canvas_centered_sprite(c, clip->frames[frame], 94 + (beat ? 3 : 0),
                                 363 - (celebrate && beat ? 8 : 0), 3u);
    if (game->collectible.mode == JELLI_COLLECTIBLE_BUBBLE) {
        int shake = beat ? 7 : 0;
        jelli_canvas_centered_sprite(c, JELLI_ASSET_HEALTH_MEDICINE, 64, 353 - shake, 2u);
        jelli_canvas_centered_sprite(c, JELLI_ASSET_HEALTH_SHOT, 134, 353 + shake, 2u);
    }
    if (lost) {
        jelli_canvas_disk(c, 72, 368, 4, 0x4e9fu);
        jelli_canvas_disk(c, 115, 371, 4, 0x4e9fu);
    }
    bool special =
        (pet->collectible_progress.special_unlocked & (1u << game->collectible.prize)) != 0u;
    if (celebrate && game->collectible.mode == JELLI_COLLECTIBLE_CHAIN)
        jelli_canvas_centered_sprite(c, JELLI_ASSET_EFFECTS_HEART, 94, 308, 1u);
    if (special)
        jelli_canvas_centered_sprite(c, JELLI_ASSET_EFFECTS_STAR, 94, 292, 1u);
}

/* Integer nearest-neighbor reuse keeps existing Jelli artwork inside a board cell. */
static void miniature(Canvas *c, const JelliAsset *asset, int x, int y)
{
    if (!asset || !asset->mask || !asset->pixels || asset->left >= asset->right ||
        asset->top >= asset->bottom || asset->right > asset->width || asset->bottom > asset->height)
        return;
    for (unsigned row = 0; row < 22u; ++row) {
        unsigned sy = asset->top + row * (unsigned)(asset->bottom - asset->top) / 22u;
        for (unsigned col = 0; col < 22u; ++col) {
            unsigned sx = asset->left + col * (unsigned)(asset->right - asset->left) / 22u;
            if (asset->mask[sy * asset->mask_stride + sx / 8u] & (0x80u >> (sx % 8u)))
                jelli_canvas_pixel(c, x - 11 + (int)col, y - 11 + (int)row,
                                   asset->pixels[sy * asset->width + sx]);
        }
    }
}

static void garden_border(Canvas *c)
{
    jelli_canvas_centered_sprite(c, JELLI_ASSET_BACKGROUNDS_KIT_TREES, 56, 177, 1u);
    jelli_canvas_centered_sprite(c, JELLI_ASSET_BACKGROUNDS_KIT_TREES, 408, 247, 1u);
    jelli_canvas_centered_sprite(c, JELLI_ASSET_PRIZES_BUTTERFLY, 68, 259, 1u);
    jelli_canvas_centered_sprite(c, JELLI_ASSET_PRIZES_RAINBOW_SEED, 400, 146, 1u);
}

static void jukebox(Canvas *c)
{
    jelli_canvas_disk(c, 233, 180, 103, 0x51a8u);
    jelli_canvas_rect(c, 130, 180, 206, 196, 0x51a8u);
    jelli_canvas_disk(c, 233, 180, 95, GOLD);
    jelli_canvas_rect(c, 138, 180, 190, 188, GOLD);
    jelli_canvas_disk(c, 233, 180, 87, TEAL);
    jelli_canvas_rect(c, 146, 180, 174, 180, TEAL);
    jelli_canvas_rect(c, 153, 266, 160, 53, BG);
    for (int y = 330; y < 358; y += 6)
        jelli_canvas_rect(c, 174, y, 118, 3, INK);
    /* Small pixel cactus beside the cabinet; rounded green arms and a terracotta pot. */
    jelli_canvas_rect(c, 379, 293, 12, 52, 0x4bcb);
    jelli_canvas_disk(c, 385, 293, 6, 0x4bcb);
    jelli_canvas_rect(c, 369, 305, 8, 20, 0x4bcb);
    jelli_canvas_rect(c, 373, 319, 12, 7, 0x4bcb);
    jelli_canvas_rect(c, 393, 299, 8, 17, 0x4bcb);
    jelli_canvas_rect(c, 387, 310, 12, 7, 0x4bcb);
    jelli_canvas_rect(c, 383, 294, 2, 44, MINT);
    jelli_canvas_rect(c, 373, 340, 25, 7, 0xb368);
    jelli_canvas_rect(c, 377, 347, 17, 15, 0x8247);
}

static void chain_draw(Canvas *c, const JelliCollectibleSession *s)
{
    garden_border(c);
    jelli_canvas_heading(c, "JELLI CHAIN", 30, 2u);
    jelli_canvas_rect(c, 99, 64, 268, 268, 0x2125u);
    for (unsigned y = 0u; y < JELLI_CHAIN_GRID; ++y)
        for (unsigned x = 0u; x < JELLI_CHAIN_GRID; ++x)
            jelli_canvas_rect(c, 101 + (int)x * 33, 66 + (int)y * 33, 32, 32, BG);
    for (unsigned i = 0u; i < s->chain.length && i < JELLI_CHAIN_LENGTH; ++i) {
        int x = 117 + (int)(s->chain.body[i] % JELLI_CHAIN_GRID) * 33;
        int y = 82 + (int)(s->chain.body[i] / JELLI_CHAIN_GRID) * 33;
        uint32_t phase = (s->elapsed_ticks / 5u + i * 17u) * UINT32_C(2654435761);
        static const JelliCreaturePose poses[] = {JELLI_POSE_IDLE, JELLI_POSE_CURIOUS,
                                                  JELLI_POSE_CONTENT, JELLI_POSE_HAPPY};
        const JelliClip *clip = jelli_creature_clip(i ? 0u : 1u, poses[(phase >> 28u) % 4u]);
        if (clip && clip->count) {
            const JelliAsset *asset = jelli_asset_lookup(
                c->assets, clip->frames[(s->elapsed_ticks / 3u + i) % clip->count]);
            miniature(c, asset, x, y);
        }
    }
    int px = 117 + (int)(s->chain.pickup % JELLI_CHAIN_GRID) * 33;
    int py = 82 + (int)(s->chain.pickup / JELLI_CHAIN_GRID) * 33;
    uint32_t pickup = s->chain.length == JELLI_CHAIN_LENGTH ? JELLI_ASSET_EFFECTS_STAR
                                                            : JELLI_ASSET_CREATURES_BABY_IDLE_A;
    miniature(c, jelli_asset_lookup(c->assets, pickup), px, py);
    char score[28];
    unsigned points = (unsigned)(s->chain.length >= 3u ? s->chain.length - 3u : 0u) * 100u +
                      (s->chain.status == JELLI_CHAIN_WON ? 500u : 0u);
    int n = snprintf(score, sizeof(score), "SCORE %u", points);
    if (n > 0 && (size_t)n < sizeof(score))
        jelli_canvas_centered(c, score, 342, 2u, PALE);
    char length[24];
    n = snprintf(length, sizeof(length), "%u JELLIS", s->chain.length);
    if (n > 0 && (size_t)n < sizeof(length))
        jelli_canvas_centered(c, length, 373, 1u, GOLD);
}

static void cafe(Canvas *c)
{
    /* A small, warmly lit vignette; the darker circular edge frames the scene. */
    jelli_canvas_disk(c, 233, 225, 218, 0x3926u);
    jelli_canvas_rect(c, 46, 92, 374, 240, 0x49a8u);
    jelli_canvas_rect(c, 46, 322, 374, 52, 0x30e5u);
    for (int x = 65; x <= 345; x += 280) {
        jelli_canvas_rect(c, x, 115, 56, 90, 0x9bceu);
        jelli_canvas_rect(c, x + 5, 120, 46, 80, 0x326bu);
        jelli_canvas_rect(c, x + 26, 120, 4, 80, 0x9bceu);
        jelli_canvas_rect(c, x + 5, 157, 46, 4, 0x9bceu);
        jelli_canvas_rect(c, x - 5, 206, 66, 5, 0xb492u);
    }
    jelli_canvas_rect(c, 52, 271, 71, 8, 0xb492u);
    jelli_canvas_rect(c, 83, 279, 8, 43, 0x7209u);
    jelli_canvas_rect(c, 74, 255, 17, 15, PALE);
    jelli_canvas_rect(c, 91, 258, 6, 8, PALE);
    jelli_canvas_rect(c, 56, 235, 59, 5, 0x7209u);
    jelli_canvas_centered_sprite(c, JELLI_ASSET_PROPS_BOOK, 85, 225, 1u);
}

static void music_notes(Canvas *c, const JelliPetUi *ui, const JelliCollectibleSession *s)
{
    unsigned clock = ui->collectible_reduced_motion ? 0u : s->record_cursor / 2205u;
    for (unsigned i = 0u; i < 3u; ++i) {
        unsigned phase = (clock + i * 11u) % 36u;
        int sway = (int)(phase < 18u ? phase : 36u - phase) / 3 - 3;
        c->dim = phase < 4u || phase > 29u;
        jelli_canvas_centered_sprite(c, JELLI_ASSET_EFFECTS_MUSIC, 193 + (int)i * 40 + sway,
                                     311 - (int)phase, 1u);
    }
    c->dim = false;
}

static void record_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    const JelliCollectibleSession *s = &game->collectible;
    cafe(c);
    jukebox(c);
    jelli_canvas_heading(c, jelli_prizes[s->prize].name, 28, 2u);
    jelli_canvas_disk(c, 233, 182, 68, INK);
    jelli_canvas_disk(c, 233, 182, 57, TEAL);
    jelli_canvas_disk(c, 233, 182, 22, GOLD);
    jelli_canvas_disk(c, 233, 182, 6, BG);
    unsigned phase = ui->collectible_reduced_motion ? 0u : s->record_cursor / 11025u % 4u;
    static const int offset[4][2] = {{0, -40}, {40, 0}, {0, 40}, {-40, 0}};
    jelli_canvas_disk(c, 233 + offset[phase][0], 182 + offset[phase][1], 5, PALE);
    music_notes(c, ui, s);
    jelli_canvas_rect(c, 181, 365, 104, 2, INK);
    unsigned progress = (unsigned)((uint64_t)s->record_cursor * 104u / JELLI_RECORD_SAMPLES);
    jelli_canvas_rect(c, 181, 365, (int)progress, 2, MINT);
}

static void paused_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    const JelliCollectibleSession *s = &game->collectible;
    const char *title = "PAUSED";
    if (s->mode == JELLI_COLLECTIBLE_CHAIN && s->chain.status != JELLI_CHAIN_RUNNING)
        title = s->chain.status == JELLI_CHAIN_WON ? "STAR CAUGHT!" : "TRY AGAIN";
    else if ((s->mode == JELLI_COLLECTIBLE_RECORD && s->record_cursor >= JELLI_RECORD_SAMPLES) ||
             (s->mode == JELLI_COLLECTIBLE_GENERIC && s->elapsed_ticks >= 80u))
        title = "LOVELY PLAY!";
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE && s->bubble.status != JELLI_BUBBLE_RUNNING)
        title = s->bubble.status == JELLI_BUBBLE_EMPTY ? "SUPPLY FINISHED" : "WELL IS FULL";
    if (s->mode == JELLI_COLLECTIBLE_RECORD && ui->result == JELLI_NOT_READY)
        title = "AUDIO NOT READY";
    jelli_canvas_rect(c, 125, 145, 216, 165, BG);
    jelli_canvas_centered(c, title, 162, 1u, GOLD);
}

void jelli_pet_collectible_session_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    const JelliCollectibleSession *s = &game->collectible;
    if (ui->collectible_win_anchor && !jelli_pet_collectible_lost(s)) {
        jelli_pet_collectible_win_draw(c, ui, game);
        controls(c, ui);
        return;
    }
    if (s->mode == JELLI_COLLECTIBLE_BUBBLE)
        jelli_pet_bubble_draw(c, s);
    else if (s->mode == JELLI_COLLECTIBLE_CHAIN)
        chain_draw(c, s);
    else if (s->mode == JELLI_COLLECTIBLE_RECORD)
        record_draw(c, ui, game);
    else {
        jelli_canvas_heading(c, "PLAY TOGETHER", 44, 2u);
        if (s->prize < jelli_prize_count) {
            jelli_canvas_centered(c, jelli_prizes[s->prize].name, 90, 1u, MINT);
            jelli_canvas_centered_sprite(c, jelli_prizes[s->prize].asset, 233, 215, 3u);
        }
        jelli_canvas_centered(c, "A LITTLE SHARED MOMENT", 298, 1u, PALE);
    }
    cheer(c, ui, game);
    if (ui->collectible_menu && !jelli_pet_collectible_lost(s))
        paused_draw(c, ui, game);
    controls(c, ui);
}
