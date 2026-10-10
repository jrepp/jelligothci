#include "jelli/wake.h"
#include "jelli/sound.h"
#include "jelli/creature.h"
#include "pet_draw.h"

/* Behaviour picks a pose; content data (generated clips) picks the frames. */
static unsigned creature_pose(const JelliPetRenderKey *v)
{
    if (v->asleep || v->reaction == 4u)
        return JELLI_POSE_ASLEEP;
    if (v->reaction == 5u)
        return v->phase == 2u ? JELLI_POSE_CURIOUS : JELLI_POSE_HAPPY;
    if (v->health == JELLI_UNWELL || v->health == JELLI_RECOVERING)
        return JELLI_POSE_UNWELL;
    /* Keep care feedback visible even when a recent touch reaction is active. */
    if (v->activity == JELLI_EATING)
        return JELLI_POSE_EATING;
    if (v->activity == JELLI_PLAYING || v->activity == JELLI_GIVING ||
        v->activity == JELLI_EXERCISING || v->reaction == 1u)
        return JELLI_POSE_HAPPY;
    if (v->reaction >= 2u)
        return JELLI_POSE_UNWELL;
    static const uint8_t idle[] = {JELLI_POSE_IDLE, JELLI_POSE_IDLE_ALT, JELLI_POSE_CURIOUS,
                                   JELLI_POSE_CONTENT};
    return idle[v->phase < 4u ? v->phase : 0u];
}

void jelli_pet_actor_clip(JelliPetUi *ui, JelliPetRenderKey *view, uint64_t time)
{
    unsigned pose = creature_pose(view);
    if (!ui->clip_started || ui->clip_pose != pose || ui->clip_form != view->form ||
        ui->clip_pet != view->active_id || time < ui->clip_anchor_ms) {
        ui->clip_anchor_ms = time; /* Each pose change restarts its clip. */
        ui->clip_pose = (uint8_t)pose;
        ui->clip_form = view->form;
        ui->clip_pet = view->active_id;
        ui->clip_started = true;
    }
    /* Clip holds are authored milliseconds, like pet.idle_frame_ms; UI scale does not apply. */
    view->pose = (uint8_t)pose;
    view->clip_frame =
        (uint8_t)jelli_clip_frame(jelli_creature_clip(view->form, pose), time - ui->clip_anchor_ms);
}

static uint32_t frame_id(const JelliPetRenderKey *v)
{
    const JelliClip *clip = jelli_creature_clip(v->form, v->pose);
    return clip && v->clip_frame < clip->count ? clip->frames[v->clip_frame] : 0u;
}

void jelli_pet_actor_layout(JelliPetUi *ui, const JelliPetRenderKey *view)
{
    uint32_t id = frame_id(view);
    if (!ui->actor_frame || ui->actor_frame->id != id ||
        (ui->rendered && ui->last_view.assets != ui->assets))
        ui->actor_frame = jelli_asset_lookup(ui->assets, id);
    const JelliAsset *a = ui->actor_frame;
    if (!a)
        return;
    bool ring = view->menu_open && view->page < JELLI_UI_BRUSH;
    unsigned anchor_x = ring ? a->centroid_x_q8 : a->ground_x_q8;
    unsigned anchor_y = ring ? a->centroid_y_q8 : a->ground_y_q8;
    int floor = view->page >= JELLI_UI_BRUSH ? 350 : 256;
    ui->actor_x = 233 - (int)((anchor_x * 6u + 128u) / 256u);
    ui->actor_y = (ring ? 233 : floor) - (int)((anchor_y * 6u + 128u) / 256u);
    ui->actor_bounds = (JelliRect){
        (unsigned)(ui->actor_x + (int)a->left * 6), (unsigned)(ui->actor_y + (int)a->top * 6),
        (unsigned)(a->right - a->left) * 6u, (unsigned)(a->bottom - a->top) * 6u};
}

uint16_t jelli_pet_background(uint8_t location, unsigned x, unsigned y)
{
    const JelliAsset *a = jelli_asset_find(location ? 10002u : 10001u);
    if (!a || x >= JELLI_WIDTH || y >= JELLI_HEIGHT)
        return 0u;
    unsigned column = x * a->width / JELLI_WIDTH;
    unsigned row = y * a->height / JELLI_HEIGHT;
    return a->pixels[row * a->width + column];
}

bool jelli_pet_touch_actor(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    const JelliPet *pet = &game->pets[game->active];
    bool activity_view = ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH;
    if ((ui->menu_open && (!activity_view || !pet->asleep)) || !ui->actor_frame ||
        x < ui->actor_x || y < ui->actor_y)
        return false;
    const JelliAsset *a = ui->actor_frame;
    unsigned column = (unsigned)(x - ui->actor_x) / 6u;
    unsigned row = (unsigned)(y - ui->actor_y) / 6u;
    if (column >= a->width || row >= a->height ||
        !(a->mask[row * a->mask_stride + column / 8u] & (1u << (7u - column % 8u))))
        return false;
    bool waking = pet->asleep;
    ui->result = jelli_game_command(
        game, (JelliCommand){waking ? JELLI_CMD_WAKE : JELLI_CMD_TOUCH, pet->id, 0u});
    if (ui->result == JELLI_OK) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
        ui->sound_pending = true;
        ui->sound_cue = JELLI_SOUND_PET + 1u;
        if (!waking || pet->wake_mood == JELLI_WAKE_HAPPY)
            jelli_particles_burst(&ui->particles, x, y, waking || pet->reaction == 1u);
    }
    return true;
}
