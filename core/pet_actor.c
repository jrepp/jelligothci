#include "pet_draw.h"

static uint32_t frame_id(const JelliPetRenderKey *v)
{
    uint32_t base = v->form == 0u ? 1000u : 1006u;
    if (v->asleep)
        return base + 5u;
    if (v->health == JELLI_UNWELL || v->health == JELLI_RECOVERING)
        return base + 6u;
    if (v->reaction >= 2u)
        return base + 6u;
    if (v->reaction == 1u)
        return base + 4u;
    if (v->activity == JELLI_EATING)
        return base + 3u;
    if (v->activity == JELLI_PLAYING || v->activity == JELLI_GIVING)
        return base + 4u;
    if (v->phase >= 2u)
        return 1021u + (v->form ? 2u : 0u) + (v->phase == 3u ? 1u : 0u);
    return base + 1u + v->phase;
}

void jelli_pet_actor_layout(JelliPetUi *ui, const JelliPetRenderKey *view)
{
    uint32_t id = frame_id(view);
    if (!ui->actor_frame || ui->actor_frame->id != id)
        ui->actor_frame = jelli_asset_find(id);
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
    const JelliAsset *a = ui->actor_frame;
    unsigned column = (unsigned)(x - ui->actor_x) / 6u;
    unsigned row = (unsigned)(y - ui->actor_y) / 6u;
    if (column >= a->width || row >= a->height ||
        !(a->mask[row * a->mask_stride + column / 8u] & (1u << (7u - column % 8u))))
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
