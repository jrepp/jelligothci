#include "pet_collection.h"
#include <stdio.h>
#include <string.h>

static unsigned selected(const JelliPetUi *ui)
{
    return ui->selected_pet >= 1u && ui->selected_pet <= 9u ? ui->selected_pet : 1u;
}

bool jelli_pet_collection_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button)
{
    if (!ui->menu_open || !slot || slot > 9u)
        return false;
    if (ui->page == JELLI_UI_PETS || ui->page == JELLI_UI_EVOLUTIONS) {
        bool forms = ui->page == JELLI_UI_EVOLUTIONS;
        if (forms && slot > 2u)
            return false;
        unsigned index = slot - 1u;
        *button = (JelliPetUiButton){
            .bounds = {95u + index % 3u * 96u, 88u + index / 3u * 96u, 84u, 84u},
            .label =
                forms ? jelli_collection_forms[index].name : jelli_collection_entries[index].name,
            .icon = forms ? jelli_collection_forms[index].portrait
                          : jelli_collection_forms[ui->last_view.pet_forms[index]].portrait,
            .scale = 2u};
        return true;
    }
    if (ui->page != JELLI_UI_PET_DETAIL || slot > 2u)
        return false;
    *button = (JelliPetUiButton){
        .bounds = {113u, slot == 1u ? 260u : 338u, 240u, 64u},
        .label = slot == 2u ? "EVOLUTIONS"
                            : (ui->last_view.active_entry == selected(ui) ? "ACTIVE" : "BRING OUT"),
        .scale = 2u};
    return true;
}

JelliResult jelli_pet_collection_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    if (ui->page != JELLI_UI_PET_DETAIL || slot != 1u)
        return JELLI_OK;
    int index = jelli_collection_find(game, selected(ui));
    if (index < 0)
        return JELLI_NOT_READY;
    return jelli_game_check(
        game, (JelliCommand){JELLI_CMD_ACTIVATE, game->pets[game->active].id, game->pets[index].id},
        &ui->action_scratch);
}

void jelli_pet_collection_select(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    ui->result = JELLI_OK;
    if (ui->page == JELLI_UI_PETS) {
        ui->selected_pet = (uint8_t)slot;
        ui->selected_form = 0u;
        ui->page = JELLI_UI_PET_DETAIL;
        uint16_t bit = (uint16_t)(1u << (slot - 1u));
        if (game->new_pets & bit) {
            game->new_pets &= (uint16_t)~bit;
            ui->save_requested = true;
            ui->save_status = JELLI_SAVE_PENDING;
        }
    } else if (ui->page == JELLI_UI_EVOLUTIONS) {
        ui->selected_form = (uint8_t)(slot - 1u);
    } else if (slot == 2u) {
        ui->page = JELLI_UI_EVOLUTIONS;
    } else {
        int index = jelli_collection_find(game, selected(ui));
        if (index < 0)
            return;
        ui->result =
            jelli_game_command(game, (JelliCommand){JELLI_CMD_ACTIVATE, game->pets[game->active].id,
                                                    game->pets[index].id});
        if (ui->result == JELLI_OK) {
            ui->menu_open = false;
            ui->page = JELLI_UI_HOME;
            ui->save_requested = true;
            ui->save_status = JELLI_SAVE_PENDING;
        }
    }
}

void jelli_pet_collection_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key)
{
    key->selected_pet = ui->selected_pet;
    key->selected_form = ui->selected_form;
    key->active_entry = game->pets[game->active].collection_entry;
    key->pets_new = game->new_pets;
    for (unsigned i = 0u; i < game->count; ++i) {
        const JelliPet *pet = &game->pets[i];
        unsigned slot = pet->collection_entry - 1u;
        key->pets_owned |= (uint16_t)(1u << slot);
        key->pet_forms[slot] = pet->form;
        key->pet_reached[slot] = (uint8_t)(pet->reached_forms | (1u << pet->form));
    }
}

bool jelli_pet_collection_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    if (a->selected_pet != b->selected_pet || a->selected_form != b->selected_form ||
        a->active_entry != b->active_entry || a->pets_new != b->pets_new ||
        a->pets_owned != b->pets_owned)
        return false;
    for (unsigned i = 0u; i < 9u; ++i)
        if (a->pet_forms[i] != b->pet_forms[i] || a->pet_reached[i] != b->pet_reached[i])
            return false;
    return true;
}

static void draw_grid(Canvas *c, const JelliPetUi *ui)
{
    const JelliPetRenderKey *v = &ui->last_view;
    bool forms = ui->page == JELLI_UI_EVOLUTIONS;
    unsigned entry = selected(ui) - 1u;
    jelli_canvas_heading(c, forms ? "EVOLUTIONS" : "PETS", 22, 3u);
    jelli_canvas_centered(c, forms ? jelli_collection_entries[entry].name : "TAP TO EXPLORE", 61,
                          1u, MINT);
    for (unsigned slot = 1u; slot <= 9u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collection_button(ui, slot, &b))
            continue;
        unsigned i = slot - 1u;
        int x = (int)b.bounds.x, y = (int)b.bounds.y;
        bool owned =
            forms ? (v->pet_reached[entry] & (1u << i)) != 0u : (v->pets_owned & (1u << i)) != 0u;
        bool active = forms ? (v->pets_owned & (1u << entry)) && v->pet_forms[entry] == i
                            : v->active_entry == slot;
        bool focus = forms ? ui->selected_form == i : ui->selected_pet == slot;
        jelli_canvas_rect(c, x, y, 84, 84, focus ? GOLD : INK);
        jelli_canvas_rect(c, x + 3, y + 3, 78, 78, BG);
        c->dim = !owned;
        jelli_canvas_centered_sprite(c, b.icon, x + 42, y + 36, 2u);
        c->dim = false;
        const char *badge = !owned                                  ? "LOCKED"
                            : active                                ? "ACTIVE"
                            : (!forms && (v->pets_new & (1u << i))) ? "NEW"
                                                                    : "OWNED";
        jelli_canvas_text(c, badge, x + 42 - (int)strlen(badge) * 4, y + 66, 1u,
                          owned ? MINT : PALE);
    }
    if (forms) {
        jelli_canvas_centered(c, jelli_collection_forms[ui->selected_form].name, 290, 2u, PALE);
        jelli_canvas_centered(c, ui->selected_form ? "GROW FROM MINT" : "STARTING FORM", 325, 1u,
                              MINT);
        char age[32];
        (void)snprintf(age, sizeof(age), "GROWTH %lu SECONDS",
                       (unsigned long)(jelli_collection_growth_ticks / 10u));
        jelli_canvas_centered(c, age, 350, 1u, PALE);
    } else if (ui->selected_pet) {
        jelli_canvas_centered(c, jelli_collection_entries[entry].name, 376, 1u, PALE);
    }
}

static void draw_detail(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned entry = selected(ui) - 1u;
    const JelliPetRenderKey *v = &ui->last_view;
    bool owned = (v->pets_owned & (1u << entry)) != 0u;
    jelli_canvas_heading(c, jelli_collection_entries[entry].name, 28, 2u);
    c->dim = !owned;
    jelli_canvas_centered_sprite(c, jelli_collection_forms[v->pet_forms[entry]].portrait, 233, 145,
                                 4u);
    c->dim = false;
    const char *hint = !owned                   ? jelli_collection_entries[entry].hint
                       : game->sleep_log.active ? "WAKE TO SWITCH PETS"
                       : game->pets[game->active].activity != JELLI_IDLE ? "FINISH ACTIVITY FIRST"
                                                                         : "YOUR COMPANION";
    jelli_canvas_centered(c, hint, 217, 1u, MINT);
    unsigned reached = v->pet_reached[entry];
    jelli_canvas_centered(c,
                          !owned          ? "DISCOVERY 0/1"
                          : reached == 3u ? "FORMS 2/2"
                                          : "FORMS 1/2",
                          239, 1u, PALE);
    for (unsigned slot = 1u; slot <= 2u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collection_button(ui, slot, &b))
            continue;
        c->dim = (v->unavailable & (1u << slot)) != 0u;
        jelli_canvas_rect(c, (int)b.bounds.x, (int)b.bounds.y, (int)b.bounds.width, 64, TEAL);
        jelli_canvas_centered(c, !owned && slot == 1u ? "LOCKED" : b.label, (int)b.bounds.y + 20,
                              2u, PALE);
        c->dim = false;
    }
}

void jelli_pet_collection_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    if (ui->page == JELLI_UI_PET_DETAIL)
        draw_detail(c, ui, game);
    else
        draw_grid(c, ui);
}
