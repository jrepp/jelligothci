#include "jelli/pet_effect.h"
#include "pet_canvas.h"
#include "pet_draw.h"
#include <string.h>

bool jelli_pet_effect_start(JelliPetUi *ui, const char *key, uint64_t animation_ms)
{
    if (!ui || !key)
        return false;
    for (unsigned i = 0u; i < jelli_effect_definition_count; ++i) {
        const JelliEffectDefinition *d = &jelli_effect_definitions[i];
        if (strcmp(key, d->key) != 0 || !jelli_effect_valid(d))
            continue;
        ui->effect = d;
        ui->effect_anchor_ms = animation_ms;
        ui->effect_bound = false;
        ui->rendered = false;
        return true;
    }
    return false;
}

void jelli_pet_effect_stop(JelliPetUi *ui)
{
    if (!ui)
        return;
    ui->effect = NULL;
    ui->effect_sample = (JelliEffectSample){0};
    ui->clip_started = false;
    ui->rendered = false;
}

void jelli_pet_effect_prepare(JelliPetUi *ui, JelliPetRenderKey *view, uint64_t time)
{
    if (!ui->effect)
        return;
    if (ui->menu_open || time < ui->effect_anchor_ms ||
        (ui->effect_bound &&
         (ui->effect_pet != view->active_id || ui->effect_form != view->form))) {
        jelli_pet_effect_stop(ui);
        return;
    }
    ui->effect_pet = view->active_id;
    ui->effect_form = view->form;
    ui->effect_bound = true;
    jelli_effect_sample(ui->effect, time - ui->effect_anchor_ms, &ui->effect_sample);
    if (!ui->effect_sample.active) {
        jelli_pet_effect_stop(ui);
        return;
    }
    view->effect_tick = (uint16_t)(time - ui->effect_anchor_ms + 1u);
    if (ui->effect_sample.has_pose) {
        view->pose = ui->effect_sample.pose;
        view->clip_frame = (uint8_t)jelli_clip_frame(jelli_creature_clip(view->form, view->pose),
                                                     ui->effect_sample.cue_elapsed_ms);
        ui->clip_started = false; /* Resume normal clip selection after each cue. */
    }
}

void jelli_pet_effect_draw(Canvas *canvas, const JelliPetUi *ui, unsigned layer)
{
    if (!ui->effect_sample.active)
        return;
    for (unsigned i = 0u; i < ui->effect_sample.count; ++i) {
        const JelliEffectSprite *sprite = &ui->effect_sample.sprites[i];
        if (sprite->layer != layer)
            continue;
        int x = sprite->x, y = sprite->y;
        if (sprite->anchor && ui->actor_frame) {
            JelliEffectPoint p = jelli_effect_point(ui->effect_form, sprite->anchor);
            int scale = ui->actor_scale;
            x += ui->actor_x +
                 (int)((ui->actor_frame->ground_x_q8 * ui->actor_scale + 128u) / 256u) +
                 p.x * scale;
            y += ui->actor_y +
                 (int)((ui->actor_frame->ground_y_q8 * ui->actor_scale + 128u) / 256u) +
                 p.y * scale;
        }
        jelli_canvas_centered_sprite(canvas, sprite->asset, x, y, sprite->scale);
    }
}
