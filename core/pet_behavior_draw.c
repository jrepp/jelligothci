#include "pet_behavior_draw.h"
#include "jelli/assets.h"
#include "jelli/creature.h"
#include "jelli/potty.h"
#include "jelli/sound.h"

#define EFFECT_TARGET_PX 40u /* Effect sprites scale to about this size above the head. */
#define FLOOR_Y 256

bool jelli_pet_mess_bounds(const JelliPetUi *ui, unsigned mess, JelliRect *bounds)
{
    if (!mess || mess > jelli_potty_rules.mess_sprite_count || !ui->actor_bounds.height)
        return false;
    const JelliAsset *a = jelli_asset_lookup(ui->assets, jelli_potty_rules.mess_sprites[mess - 1u]);
    if (!a)
        return false;
    /* Same pixel scale as the pet, so the mess reads as part of its world; bottom on the floor. */
    unsigned scale = ui->actor_scale ? ui->actor_scale : 1u;
    int width = (int)(a->width * scale), height = (int)(a->height * scale);
    int x = (int)(ui->actor_bounds.x + ui->actor_bounds.width) - width / 4;
    if (x + width > JELLI_WIDTH - 70)
        x = (int)ui->actor_bounds.x - width * 3 / 4;
    int y = FLOOR_Y - (int)(a->bottom * scale);
    if (x < 0 || y < 0)
        return false;
    *bounds = (JelliRect){(unsigned)x, (unsigned)y, (unsigned)width, (unsigned)height};
    return true;
}

/* The mess shimmers through its authored frames beside the pet. */
static void draw_mess(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v)
{
    JelliRect r;
    if (!jelli_pet_mess_bounds(ui, v->mess, &r))
        return;
    unsigned scale = ui->actor_scale ? ui->actor_scale : 1u;
    int x = (int)r.x;
    unsigned brooms = jelli_potty_rules.broom_sprite_count;
    if (v->sweep && brooms) { /* Pushed one and a half widths aside while the broom swishes. */
        unsigned steps = jelli_potty_rules.sweep_steps;
        x += (int)((v->sweep - 1u) * r.width * 3u / 2u / (steps - 1u));
        uint32_t broom = jelli_potty_rules.broom_sprites[v->sweep % brooms];
        jelli_canvas_sprite(c, broom, x - (int)r.width * 2 / 3, (int)r.y, scale);
    }
    jelli_canvas_sprite(c, jelli_potty_rules.mess_sprites[v->mess - 1u], x, (int)r.y, scale);
}

bool jelli_pet_tap_mess(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    JelliRect r;
    if (ui->menu_open || !jelli_pet_mess_bounds(ui, ui->last_view.mess, &r) || x < (int)r.x ||
        y < (int)r.y || x >= (int)(r.x + r.width) || y >= (int)(r.y + r.height))
        return false;
    /* Tapping the mess cleans it up: the same CLEAN command as Care > Clean. */
    const JelliPet *pet = &game->pets[game->active];
    ui->result = jelli_game_command(game, (JelliCommand){JELLI_CMD_CLEAN, pet->id, 0u});
    if (ui->result == JELLI_OK) {
        ui->sound_pending = true;
        ui->sound_cue = JELLI_SOUND_CONFIRM + 1u;
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
    return true;
}

void jelli_pet_draw_behavior(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v)
{
    draw_mess(c, ui, v);
    const JelliBehaviorLook *look = v->behavior ? jelli_behavior_look(v->behavior - 1u) : NULL;
    if (!look || !ui->actor_bounds.height)
        return;
    int x = (int)(ui->actor_bounds.x + ui->actor_bounds.width / 2u);
    if (look->prop) {
        int y = (int)(ui->actor_bounds.y + ui->actor_bounds.height * 2u / 3u);
        jelli_canvas_centered_sprite(c, look->prop, x, y, 3u);
    }
    if (look->effect) {
        int bob = (v->clip_frame & 1u) ? 4 : 0; /* Follows the clip, so no extra redraws. */
        int y = (int)ui->actor_bounds.y - 22 - bob;
        jelli_canvas_centered_sprite(c, look->effect, x + (int)ui->actor_bounds.width / 3, y,
                                     jelli_canvas_fit_scale(look->effect, EFFECT_TARGET_PX));
    }
}

const char *jelli_pet_behavior_caption(const JelliPetRenderKey *v)
{
    if (v->sweep)
        return "SWEEPING UP";
    if (v->mess)
        return "TAP TO CLEAN UP"; /* The mess outranks moods: it says how to fix it. */
    const JelliBehaviorLook *look = v->behavior ? jelli_behavior_look(v->behavior - 1u) : NULL;
    return look && look->caption ? look->caption : "";
}
