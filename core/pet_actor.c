#include "pet_draw.h"

typedef enum {
    POSE_IDLE_A,
    POSE_IDLE_B,
    POSE_EATING,
    POSE_HAPPY,
    POSE_ASLEEP,
    POSE_UNWELL,
    POSE_CURIOUS,
    POSE_CONTENT,
    POSE_COUNT
} Pose;

/* Stable asset IDs per form, in pose order (assets/slice/assets.json). */
static const uint32_t pose_ids[JELLI_FORM_COUNT][POSE_COUNT] = {
    {1001u, 1002u, 1003u, 1004u, 1005u, 1006u, 1021u, 1022u},
    {1007u, 1008u, 1009u, 1010u, 1011u, 1012u, 1023u, 1024u},
    {1031u, 1032u, 1033u, 1034u, 1035u, 1036u, 1037u, 1038u},
    {1041u, 1042u, 1043u, 1044u, 1045u, 1046u, 1047u, 1048u}};

static Pose pose(const JelliPetRenderKey *v)
{
    if (v->asleep)
        return POSE_ASLEEP;
    if (v->health == JELLI_UNWELL || v->health == JELLI_RECOVERING)
        return POSE_UNWELL;
    /* Keep care feedback visible even when a recent touch reaction is active. */
    if (v->activity == JELLI_EATING)
        return POSE_EATING;
    if (v->activity == JELLI_PLAYING || v->activity == JELLI_GIVING)
        return POSE_HAPPY;
    if (v->reaction >= 2u)
        return POSE_UNWELL;
    if (v->reaction == 1u)
        return POSE_HAPPY;
    if (v->phase >= 2u)
        return v->phase == 3u ? POSE_CONTENT : POSE_CURIOUS;
    return v->phase ? POSE_IDLE_B : POSE_IDLE_A;
}

static uint32_t frame_id(const JelliPetRenderKey *v)
{
    /* Saves are validated, but never index past the table: unknown forms draw as Mint. */
    unsigned form = v->form < JELLI_FORM_COUNT ? v->form : JELLI_FORM_MINT;
    return pose_ids[form][pose(v)];
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
    if (ui->menu_open || !ui->actor_frame || x < ui->actor_x || y < ui->actor_y)
        return false;
    /* The actor is drawn at 6x; sample the stored coverage beneath the touch. */
    if (!jelli_asset_hit(ui->actor_frame, (unsigned)(x - ui->actor_x), (unsigned)(y - ui->actor_y),
                         6u))
        return false;
    const JelliPet *pet = &game->pets[game->active];
    ui->result = jelli_game_command(game, (JelliCommand){JELLI_CMD_TOUCH, pet->id, 0u});
    if (ui->result == JELLI_OK) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
        ui->sound_pending = pet->reaction == 1u;
        jelli_particles_burst(&ui->particles, x, y, pet->reaction == 1u);
    }
    return true;
}
