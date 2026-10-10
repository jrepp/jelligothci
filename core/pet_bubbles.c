#include "pet_draw.h"

static void clear_bubbles(JelliParticles *pool)
{
    for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i) {
        if ((pool->items[i].style & JELLI_PARTICLE_BUBBLE) && pool->items[i].life) {
            pool->items[i].life = 0u;
            pool->changed = true;
        }
    }
}

static void emit_bubbles(JelliPetUi *ui, bool brushing)
{
    const JelliAsset *asset = ui->actor_frame;
    JelliParticles *pool = &ui->particles;
    int mouth_x =
        ui->actor_x + (int)((asset->centroid_x_q8 * (unsigned)ui->actor_scale + 128u) / 256u);
    int mouth_y = ui->actor_y +
                  (int)((asset->centroid_y_q8 * (unsigned)ui->actor_scale + 128u) / 256u) -
                  (int)ui->actor_bounds.height / 4;
    for (unsigned i = 0; i < (brushing ? 2u : 4u); ++i) {
        unsigned phase = pool->next;
        int x = brushing ? mouth_x - 5 + (int)i * 10
                         : (int)(ui->actor_bounds.x + phase * 37u % ui->actor_bounds.width);
        int y = brushing ? mouth_y : (int)ui->actor_bounds.y;
        if (x < 0 || y < 0 || x >= JELLI_WIDTH || y >= JELLI_HEIGHT)
            continue;
        pool->items[pool->next] = (JelliParticle){
            .x = (int16_t)(x * 16),
            .y = (int16_t)(y * 16),
            .vx = (int8_t)((int)(phase % 5u) - 2),
            .vy = (int8_t)(brushing ? -12 : 36 + (int)(phase % 5u)),
            .life = (uint8_t)(brushing ? 30u : 40u),
            .style = (uint8_t)(JELLI_PARTICLE_BUBBLE | (brushing ? JELLI_PARTICLE_RISE : 0u) |
                               (phase % 3u))};
        pool->next = (uint8_t)((pool->next + 1u) % JELLI_PARTICLE_CAPACITY);
        pool->changed = true;
    }
}

void jelli_pet_bubbles(JelliPetUi *ui, bool asleep, uint64_t time)
{
    unsigned mode = !ui->menu_open || ui->clicker_done || asleep ? 0u
                    : ui->page == JELLI_UI_BRUSH                 ? 1u
                    : ui->page == JELLI_UI_WASH                  ? 2u
                                                                 : 0u;
    if (mode != ui->bubble_mode) {
        clear_bubbles(&ui->particles);
        ui->bubble_mode = (uint8_t)mode;
        ui->bubble_emitted = false;
    }
    if (!mode || !ui->actor_frame || !ui->actor_bounds.width)
        return;
    uint64_t interval = mode == 1u ? 160u : 100u;
    if (ui->bubble_emitted && time >= ui->bubble_emit_ms && time - ui->bubble_emit_ms < interval)
        return;
    /* One bounded batch after a stall; injected animation time also honors pause. */
    emit_bubbles(ui, mode == 1u);
    ui->bubble_emit_ms = time;
    ui->bubble_emitted = true;
}
