#include "jelli/wake.h"
#include "jelli/sound.h"
#include "jelli/creature.h"
#include "pet_draw.h"

/* Code defines what each condition means; content data orders them into poses. */
static bool condition_holds(unsigned when, const JelliPetRenderKey *v)
{
    switch ((JelliCreatureCondition)when) {
    case JELLI_WHEN_ASLEEP:
        return v->asleep;
    case JELLI_WHEN_WAKE_GROGGY:
        return v->reaction == JELLI_REACTION_WAKE_GROGGY;
    case JELLI_WHEN_WAKE_SURPRISED:
        return v->reaction == JELLI_REACTION_WAKE_HAPPY && v->phase == JELLI_POSE_CURIOUS;
    case JELLI_WHEN_WAKE_HAPPY:
        return v->reaction == JELLI_REACTION_WAKE_HAPPY;
    case JELLI_WHEN_UNWELL:
        return v->health == JELLI_UNWELL || v->health == JELLI_RECOVERING;
    case JELLI_WHEN_EATING:
        return v->activity == JELLI_EATING;
    case JELLI_WHEN_PLAYING:
        return v->activity == JELLI_PLAYING || v->activity == JELLI_GIVING ||
               v->activity == JELLI_EXERCISING;
    case JELLI_WHEN_TOUCH_HAPPY:
        return v->reaction == JELLI_REACTION_TOUCH_HAPPY;
    case JELLI_WHEN_TOUCH_UPSET:
        return v->reaction == JELLI_REACTION_TOUCH_UPSET ||
               v->reaction == JELLI_REACTION_TOUCH_OVERLOAD;
    case JELLI_WHEN_COUNT:
        break;
    }
    return false;
}

static unsigned creature_pose(const JelliCreatureProfile *profile, const JelliPetRenderKey *v)
{
    unsigned count = profile->rule_count < JELLI_POSE_RULE_CAPACITY ? profile->rule_count
                                                                    : JELLI_POSE_RULE_CAPACITY;
    for (unsigned i = 0u; i < count; ++i)
        if (condition_holds(profile->rules[i].when, v))
            return profile->rules[i].pose;
    /* Then a behaviour state's pose (content/creatures.json), then the idle schedule. */
    const JelliBehaviorLook *look = v->behavior ? jelli_behavior_look(v->behavior - 1u) : NULL;
    if (look && look->pose < jelli_creature_pose_count)
        return look->pose;
    return v->phase <= JELLI_POSE_CONTENT ? v->phase : JELLI_POSE_IDLE;
}

void jelli_pet_actor_clip(JelliPetUi *ui, JelliPetRenderKey *view, uint64_t time)
{
    unsigned pose = creature_pose(jelli_creature_profile(view->form), view);
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
    unsigned scale = jelli_creature_profile(view->form)->scale;
    ui->actor_scale = (uint8_t)scale;
    ui->actor_x = 233 - (int)((anchor_x * scale + 128u) / 256u);
    ui->actor_y = (ring ? 233 : floor) - (int)((anchor_y * scale + 128u) / 256u);
    ui->actor_bounds =
        (JelliRect){(unsigned)(ui->actor_x + (int)(a->left * scale)),
                    (unsigned)(ui->actor_y + (int)(a->top * scale)),
                    (unsigned)(a->right - a->left) * scale, (unsigned)(a->bottom - a->top) * scale};
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
    bool activity_view = jelli_pet_page_is_routine(ui->page);
    if ((ui->menu_open && (!activity_view || !pet->asleep)) || !ui->actor_frame ||
        x < ui->actor_x || y < ui->actor_y)
        return false;
    const JelliAsset *a = ui->actor_frame;
    unsigned scale = ui->actor_scale ? ui->actor_scale : 1u;
    unsigned column = (unsigned)(x - ui->actor_x) / scale;
    unsigned row = (unsigned)(y - ui->actor_y) / scale;
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
            jelli_particles_burst(&ui->particles, x, y,
                                  waking || pet->reaction == JELLI_REACTION_TOUCH_HAPPY);
    }
    return true;
}
