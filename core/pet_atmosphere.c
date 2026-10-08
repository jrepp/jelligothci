#include "pet_draw.h"

uint16_t jelli_pet_night_color(uint16_t color, uint8_t night)
{
    /* A mild cool tint retains readable, colorful icons at night. */
    unsigned amount = night / 4u;
    unsigned r = ((color >> 11) * (255u - amount) + 8u * amount) / 255u;
    unsigned g = (((color >> 5) & 63u) * (255u - amount) + 20u * amount) / 255u;
    unsigned b = ((color & 31u) * (255u - amount) + 16u * amount) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void jelli_pet_atmosphere(JelliPetUi *ui, const JelliPet *pet, uint64_t time,
                          JelliPetRenderKey *view)
{
    unsigned minute = jelli_pet_clock_minute(ui, pet);
    uint8_t target = minute >= 1200u || minute < 360u ? 255u : 0u;
    if (!ui->atmosphere_ready) {
        ui->night_from = target;
        ui->night_target = target;
        ui->night_anchor_ms = time;
        ui->atmosphere_ready = true;
    }
    if (target != ui->night_target || time < ui->night_anchor_ms) {
        ui->night_from = ui->last_view.night;
        ui->night_target = target;
        ui->night_anchor_ms = time;
    }
    uint32_t duration = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_NIGHT_MS);
    uint64_t elapsed = time - ui->night_anchor_ms;
    if (elapsed >= duration) {
        view->night = target;
    } else {
        int delta = (int)target - ui->night_from;
        view->night =
            (uint8_t)((int)ui->night_from + delta * (int)(elapsed / 100u * 100u) / (int)duration);
    }
}

void jelli_pet_sleep_particles(JelliPetUi *ui, bool asleep, uint64_t time)
{
    JelliParticles *pool = &ui->particles;
    if (!asleep) {
        for (unsigned i = 0; i < JELLI_PARTICLE_CAPACITY; ++i) {
            if ((pool->items[i].style & 64u) && pool->items[i].life) {
                pool->items[i].life = 0;
                pool->changed = true;
            }
        }
        ui->sleep_emitted = false;
        return;
    }
    if (!ui->actor_frame ||
        (ui->sleep_emitted && time >= ui->sleep_emit_ms && time - ui->sleep_emit_ms < 900u))
        return;
    const JelliAsset *a = ui->actor_frame;
    int x = ui->actor_x + (int)((a->centroid_x_q8 * 6u + 128u) / 256u);
    int y = ui->actor_y + (int)((a->centroid_y_q8 * 6u + 128u) / 256u) -
            (int)ui->actor_bounds.height / 2;
    if (x < 0 || y < 0 || x >= JELLI_WIDTH || y >= JELLI_HEIGHT)
        return;
    pool->items[pool->next] = (JelliParticle){.x = (int16_t)(x * 16),
                                              .y = (int16_t)(y * 16),
                                              .vx = 10,
                                              .vy = -22,
                                              .life = 32,
                                              .style = (uint8_t)(64u | ((pool->next & 1u) << 3))};
    pool->next = (uint8_t)((pool->next + 1u) % JELLI_PARTICLE_CAPACITY);
    pool->changed = true;
    ui->sleep_emit_ms = time;
    ui->sleep_emitted = true;
}

void jelli_pet_draw_background(JelliSurface *s, const JelliPetRenderKey *view, JelliRect region)
{
    const JelliAsset *a = jelli_asset_lookup(view->assets, view->location ? 10002u : 10001u);
    if (!a)
        return;
    for (unsigned y = region.y; y < region.y + region.height; ++y) {
        for (unsigned x = region.x; x < region.x + region.width; ++x) {
            int rx = 2 * (int)x - 465, ry = 2 * (int)y - 465;
            uint16_t color =
                a->pixels[(y * a->height / JELLI_HEIGHT) * a->width + x * a->width / JELLI_WIDTH];
            unsigned shade = 256u - view->night / 2u;
            unsigned r = (color >> 11) * shade >> 8;
            unsigned g = ((color >> 5) & 63u) * shade >> 8;
            unsigned b = (color & 31u) * shade >> 8;
            int mx = (int)x - 233, my = (int)y - 126;
            int cutx = (int)x - 251, cuty = (int)y - 113;
            if (view->night && mx * mx + my * my <= 42 * 42 &&
                cutx * cutx + cuty * cuty > 38 * 38) {
                r = (r * (256u - view->night) + 23u * view->night) >> 8;
                g = (g * (256u - view->night) + 46u * view->night) >> 8;
                b = (b * (256u - view->night) + 25u * view->night) >> 8;
            }
            s->pixels[y * s->stride + x] =
                rx * rx + ry * ry <= 466 * 466 ? (uint16_t)((r << 11) | (g << 5) | b) : 0u;
        }
    }
}
