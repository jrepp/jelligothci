#include "pet_behavior_draw.h"
#include "jelli/assets.h"
#include "jelli/creature.h"
#include "jelli/potty.h"

#define EFFECT_TARGET_PX 40u /* Effect sprites scale to about this size above the head. */
#define MESS_SCALE 2u
#define FLOOR_Y 256

static unsigned fit_scale(const JelliPetUi *ui, uint32_t id, unsigned target)
{
    const JelliAsset *a = jelli_asset_lookup(ui->assets, id);
    unsigned scale = a && a->width ? target / a->width : 1u;
    return scale ? scale : 1u;
}

/* The mess sits on the floor beside the pet and shimmers through its authored frames. */
static void draw_mess(Canvas *c, const JelliPetUi *ui, const JelliPetRenderKey *v)
{
    if (!v->mess || v->mess > jelli_potty_rules.mess_sprite_count)
        return;
    uint32_t id = jelli_potty_rules.mess_sprites[v->mess - 1u];
    const JelliAsset *a = jelli_asset_lookup(ui->assets, id);
    if (!a)
        return;
    int x = (int)(ui->actor_bounds.x + ui->actor_bounds.width) + 40;
    if (x > JELLI_WIDTH - 90)
        x = (int)ui->actor_bounds.x - 40;
    jelli_canvas_centered_sprite(c, id, x, FLOOR_Y - (int)(a->height * MESS_SCALE) / 2, MESS_SCALE);
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
                                     fit_scale(ui, look->effect, EFFECT_TARGET_PX));
    }
}

const char *jelli_pet_behavior_caption(const JelliPetRenderKey *v)
{
    const JelliBehaviorLook *look = v->behavior ? jelli_behavior_look(v->behavior - 1u) : NULL;
    if (look && look->caption && look->caption[0])
        return look->caption;
    return v->mess ? "OOPS! CLEAN UP" : "";
}
