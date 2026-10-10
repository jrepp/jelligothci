#include "pet_collection.h"
#include "jelli/creature.h"
#include <stdio.h>
#include <string.h>

static unsigned selected(const JelliPetUi *ui)
{
    return ui->selected_pet >= 1u && ui->selected_pet <= 9u ? ui->selected_pet : 1u;
}

static const JelliEvolutionSet *selected_set(const JelliPetUi *ui)
{
    return jelli_collection_set(selected(ui));
}

static unsigned reached_count(const JelliEvolutionSet *set, unsigned reached)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < set->form_count; ++i)
        count += (reached >> set->forms[i]) & 1u;
    return count;
}

bool jelli_pet_collection_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *button)
{
    if (!ui->menu_open || !slot || slot > 9u)
        return false;
    if (ui->page == JELLI_UI_PETS || ui->page == JELLI_UI_EVOLUTIONS) {
        bool forms = ui->page == JELLI_UI_EVOLUTIONS;
        const JelliEvolutionSet *set = selected_set(ui);
        if (forms && slot > set->form_count)
            return false;
        unsigned index = slot - 1u;
        unsigned form = forms ? jelli_evolution_form(set, index) : ui->last_view.pet_forms[index];
        *button =
            (JelliPetUiButton){.bounds = {95u + index % 3u * 96u, 88u + index / 3u * 96u, 84u, 84u},
                               .label = forms ? jelli_collection_forms[form].name
                                              : jelli_collection_entries[index].name,
                               .icon = jelli_collection_forms[form].portrait,
                               .scale = jelli_creature_profile(form)->icon_scale};
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
    int index = jelli_collection_find(game, selected(ui));
    if (ui->page == JELLI_UI_EVOLUTIONS)
        return index < 0
                   ? JELLI_NOT_READY
                   : jelli_game_check(
                         game, (JelliCommand){JELLI_CMD_FORM, game->pets[index].id, slot - 1u},
                         &ui->action_scratch);
    if (ui->page != JELLI_UI_PET_DETAIL || slot != 1u)
        return JELLI_OK;
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
        int index = jelli_collection_find(game, selected(ui));
        if (index < 0)
            return;
        ui->result = jelli_game_command(
            game, (JelliCommand){JELLI_CMD_FORM, game->pets[index].id, slot - 1u});
        if (ui->result == JELLI_OK) {
            ui->selected_form = (uint8_t)(slot - 1u);
            ui->save_requested = true;
            ui->save_status = JELLI_SAVE_PENDING;
        }
    } else if (slot == 2u) {
        ui->page = JELLI_UI_EVOLUTIONS;
        int index = jelli_collection_find(game, selected(ui));
        int position =
            index < 0 ? 0 : jelli_evolution_index(selected_set(ui), game->pets[index].form);
        ui->selected_form = (uint8_t)(position < 0 ? 0 : position);
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
    for (unsigned entry = 1u; entry <= 9u; ++entry) /* Locked pets show their starting form. */
        key->pet_forms[entry - 1u] = jelli_collection_set(entry)->forms[0];
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

static void draw_form_caption(Canvas *c, const JelliPetUi *ui, const JelliEvolutionSet *set)
{
    unsigned index =
        ui->result == JELLI_NOT_READY && ui->attempted_slot == 2u ? 1u : ui->selected_form;
    if (index >= set->form_count)
        index = 0u;
    jelli_canvas_centered(c, jelli_collection_forms[jelli_evolution_form(set, index)].name, 290, 2u,
                          PALE);
    if (set->form_count < 2u) {
        jelli_canvas_centered(c, "ONE FORM SPECIES", 325, 1u, MINT);
        return;
    }
    char text[32];
    (void)snprintf(text, sizeof(text), "GROW FROM %s", jelli_collection_forms[set->forms[0]].name);
    jelli_canvas_centered(c, index ? text : "STARTING FORM", 325, 1u, MINT);
    (void)snprintf(text, sizeof(text), "GROWTH %lu SECONDS",
                   (unsigned long)(set->growth_ticks / 10u));
    jelli_canvas_centered(c, text, 350, 1u, PALE);
}

static void draw_grid(Canvas *c, const JelliPetUi *ui)
{
    const JelliPetRenderKey *v = &ui->last_view;
    bool forms = ui->page == JELLI_UI_EVOLUTIONS;
    unsigned entry = selected(ui) - 1u;
    const JelliEvolutionSet *set = selected_set(ui);
    jelli_canvas_heading(c, forms ? "EVOLUTIONS" : "PETS", 22, 3u);
    jelli_canvas_centered(c, forms ? "TAP TO SWITCH FORM" : "TAP TO EXPLORE", 61, 1u, MINT);
    for (unsigned slot = 1u; slot <= 9u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collection_button(ui, slot, &b))
            continue;
        unsigned i = slot - 1u;
        int x = (int)b.bounds.x, y = (int)b.bounds.y;
        unsigned form = forms ? jelli_evolution_form(set, i) : 0u;
        bool owned = forms ? (v->pet_reached[entry] & (1u << form)) != 0u
                           : (v->pets_owned & (1u << i)) != 0u;
        bool active = forms ? (v->pets_owned & (1u << entry)) && v->pet_forms[entry] == form
                            : v->active_entry == slot;
        bool focus = forms ? ui->selected_form == i : ui->selected_pet == slot;
        jelli_canvas_rect(c, x, y, 84, 84, focus ? GOLD : INK);
        jelli_canvas_rect(c, x + 3, y + 3, 78, 78, BG);
        c->dim = !owned;
        jelli_canvas_centered_sprite(c, b.icon, x + 42, y + 36, b.scale);
        c->dim = false;
        const char *badge = !owned                                  ? "LOCKED"
                            : active                                ? "ACTIVE"
                            : (!forms && (v->pets_new & (1u << i))) ? "NEW"
                                                                    : "OWNED";
        jelli_canvas_text(c, badge, x + 42 - (int)strlen(badge) * 4, y + 4, 1u,
                          owned ? MINT : PALE);
        const char *name = b.label;
        jelli_canvas_text(c, name, x + 42 - (int)strlen(name) * 4, y + 66, 1u, PALE);
    }
    if (forms)
        draw_form_caption(c, ui, set);
    else if (ui->selected_pet)
        jelli_canvas_centered(c, jelli_collection_entries[entry].name, 376, 1u, PALE);
}

static void draw_detail(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned entry = selected(ui) - 1u;
    const JelliPetRenderKey *v = &ui->last_view;
    bool owned = (v->pets_owned & (1u << entry)) != 0u;
    jelli_canvas_heading(c, jelli_collection_entries[entry].name, 28, 2u);
    c->dim = !owned;
    unsigned form = v->pet_forms[entry];
    jelli_canvas_centered_sprite(c, jelli_collection_forms[form].portrait, 233, 145,
                                 jelli_creature_profile(form)->portrait_scale);
    c->dim = false;
    const char *hint = !owned                   ? jelli_collection_entries[entry].hint
                       : game->sleep_log.active ? "WAKE TO SWITCH PETS"
                       : game->pets[game->active].activity != JELLI_IDLE ? "FINISH ACTIVITY FIRST"
                                                                         : "YOUR COMPANION";
    jelli_canvas_caption(c, hint, 209, PALE);
    const JelliEvolutionSet *set = jelli_collection_set(entry + 1u);
    char forms[24];
    (void)snprintf(forms, sizeof(forms), "FORMS %u/%u", reached_count(set, v->pet_reached[entry]),
                   (unsigned)set->form_count);
    jelli_canvas_centered(c, !owned ? "DISCOVERY 0/1" : forms, 239, 1u, PALE);
    for (unsigned slot = 1u; slot <= 2u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collection_button(ui, slot, &b))
            continue;
        bool disabled = (v->unavailable & (1u << slot)) != 0u;
        jelli_canvas_rect(c, (int)b.bounds.x, (int)b.bounds.y, (int)b.bounds.width, 64,
                          disabled ? INK : TEAL);
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
