#include "jelli/pet_ui.h"
#include "jelli/invitations.h"
#include "jelli/activities.h"

bool jelli_pet_special_active(const JelliPetUi *ui, unsigned slot)
{
    return jelli_pet_location_active(ui, slot) || jelli_pet_quick_active(ui, slot);
}

JelliResult jelli_pet_special_available(JelliPetUi *ui, const JelliGame *game, unsigned slot)
{
    return ui->location_menu ? jelli_pet_location_available(ui, game, slot)
                             : jelli_pet_quick_available(ui, game, slot);
}

bool jelli_pet_discovery_badge(const JelliPetUi *ui, unsigned page, unsigned slot)
{
    return page == JELLI_UI_MOMENTS && !ui->quick_menu && !ui->location_menu && slot < 6u;
}

bool jelli_pet_discovery_control(const JelliPetUi *ui, unsigned slot, bool asleep,
                                 JelliPetUiButton *button)
{
    if (ui->location_menu)
        return jelli_pet_location_button(ui, slot, asleep, button);
    if (ui->quick_menu)
        return jelli_pet_quick_button(ui, slot, asleep, button);
    return ui->page == JELLI_UI_HOME ? jelli_pet_home_button(ui, slot, asleep, button)
                                     : jelli_pet_discovery_button(ui, slot, asleep, button);
}

bool jelli_pet_discovery_slot(const JelliPetUi *ui, unsigned slot)
{
    return slot >= 1u && slot <= 6u &&
           (ui->page == JELLI_UI_MOMENTS ||
            (ui->page == JELLI_UI_HOME && (slot == 1u || slot == 6u)));
}

JelliCommand jelli_pet_activity_command(const JelliGame *game, unsigned choice)
{
    JelliCommandKind kind = choice == jelli_moment_count        ? JELLI_CMD_EXERCISE
                            : choice == jelli_moment_count + 1u ? JELLI_CMD_PLAY
                                                                : JELLI_CMD_MOMENT;
    return (JelliCommand){kind, game->pets[game->active].id,
                          kind == JELLI_CMD_MOMENT ? choice : 0u};
}

unsigned jelli_pet_activity_pages(const JelliPetUi *ui)
{
    unsigned count = ui->activity_snapshot ? ui->activity_count : jelli_moment_count + 2u;
    return count ? (count + 4u) / 5u : 1u;
}

static bool ready(JelliPetUi *ui, const JelliGame *game, unsigned id)
{
    return jelli_game_check(game, jelli_pet_activity_command(game, id), &ui->action_scratch) ==
           JELLI_OK;
}

static void snapshot(JelliPetUi *ui, const JelliGame *game)
{
    ui->activity_count = 0u;
    ui->activity_offers = ui->ready_offers;
    /* Snapshot order for this visit. Expiry disables the same target; it never swaps it. */
    uint32_t available = 0u;
    for (unsigned id = 0u; id < jelli_moment_count; ++id)
        if (ready(ui, game, id))
            available |= UINT32_C(1) << id;
    for (unsigned group = 0u; group < 2u; ++group) {
        if (group == 1u) {
            if (ready(ui, game, jelli_moment_count + 1u))
                ui->activity_choices[ui->activity_count++] = (uint8_t)(jelli_moment_count + 1u);
            if (ready(ui, game, jelli_moment_count))
                ui->activity_choices[ui->activity_count++] = (uint8_t)jelli_moment_count;
        }
        for (unsigned id = 0u; id < jelli_moment_count; ++id) {
            uint32_t bit = UINT32_C(1) << id;
            unsigned rank = (ui->ready_offers & bit) ? 0u : (available & bit) ? 1u : 2u;
            if (rank == group)
                ui->activity_choices[ui->activity_count++] = (uint8_t)id;
        }
    }
    ui->activity_page = 0u;
    ui->activity_snapshot = true;
}

static void acknowledge(JelliPetUi *ui, JelliGame *game)
{
    if (!ui->menu_open || ui->quick_menu || ui->location_menu || ui->page != JELLI_UI_MOMENTS ||
        ui->last_view.ring_moving || ui->last_view.ring_page != JELLI_UI_MOMENTS ||
        ui->last_view.activity_page != ui->activity_page || !ui->last_view.ring_visible)
        return;
    for (unsigned slot = 1u; slot <= 5u; ++slot) {
        unsigned id = jelli_pet_activity_choice(ui, slot);
        if (id < jelli_moment_count && (ui->ready_offers & (UINT32_C(1) << id)) &&
            jelli_invitation_seen(&game->pets[game->active], id)) {
            ++game->revision;
            ui->save_requested = true;
            ui->save_status = JELLI_SAVE_PENDING;
        }
    }
}

static void cycle_icon(JelliPetUi *ui, uint64_t now)
{
    unsigned count = 0u;
    for (unsigned i = 0; i < jelli_moment_count; ++i)
        if (ui->unseen_offers & (UINT32_C(1) << i))
            ++count;
    if (!count)
        return;
    unsigned choice = (unsigned)((now - ui->discovery_ms) / 2400u % count);
    for (unsigned i = 0; i < jelli_moment_count; ++i)
        if ((ui->unseen_offers & (UINT32_C(1) << i)) && choice-- == 0u) {
            ui->discovery_icon = (uint8_t)(i + 1u);
            break;
        }
    if (now - ui->discovery_ms < 2400u)
        ui->discovery_pulse = (uint8_t)(1u + (now - ui->discovery_ms) / 300u % 2u);
}

static void discovery_cue(JelliPetUi *ui, const JelliGame *game, uint64_t now, uint32_t old_unseen)
{
    const JelliPet *pet = &game->pets[game->active];
    if (ui->unseen_offers != old_unseen || ui->discovery_pet != pet->id || now < ui->discovery_ms)
        ui->discovery_ms = now;
    if (ui->menu_open && ui->page == JELLI_UI_HOME &&
        (!ui->last_view.menu_open || ui->last_view.page != JELLI_UI_HOME))
        ui->discovery_ms = now;
    ui->discovery_pet = pet->id;
    ui->discovery_icon = ui->discovery_pulse = 0u;
    if (ui->effect || game->resuming || game->collectible.active || pet->asleep ||
        pet->activity != JELLI_IDLE || !ui->unseen_offers)
        return;
    cycle_icon(ui, now);
}

void jelli_pet_discovery_refresh(JelliPetUi *ui, JelliGame *game, uint64_t now)
{
    const JelliPet *pet = &game->pets[game->active];
    uint32_t old_unseen = ui->unseen_offers;
    ui->ready_offers = 0u;
    uint8_t basis;
    uint64_t minute = jelli_invitation_clock(game, pet, &basis);
    if (!game->resuming && minute >= pet->invitations.clock_high &&
        basis == pet->invitations.clock_basis) {
        for (unsigned i = 0; i < jelli_moment_count; ++i)
            if ((pet->invitations.pending & (UINT32_C(1) << i)) && ready(ui, game, i))
                ui->ready_offers |= UINT32_C(1) << i;
    }
    if (!ui->menu_open || ui->page != JELLI_UI_MOMENTS || ui->discovery_pet != pet->id)
        ui->activity_snapshot = false;
    if (ui->menu_open && ui->page == JELLI_UI_MOMENTS && !ui->activity_snapshot)
        snapshot(ui, game);
    jelli_pet_home_refresh(ui, game);
    acknowledge(ui, game);
    ui->unseen_offers = ui->ready_offers & ~pet->invitations.seen;
    discovery_cue(ui, game, now, old_unseen);
}

bool jelli_pet_discovery_button(const JelliPetUi *ui, unsigned slot, bool asleep,
                                JelliPetUiButton *button)
{
    if (!slot || slot > 6u)
        return false;
    if (slot == 6u)
        return jelli_pet_activity_pages(ui) > 1u &&
               jelli_pet_activity_button(0u, slot, asleep, button);
    unsigned id = jelli_pet_activity_choice(ui, slot);
    if (!jelli_pet_activity_button(id / 5u, id % 5u + 1u, asleep, button))
        return false;
    JelliPetUiButton position;
    if (!jelli_pet_ui_button(JELLI_UI_MOMENTS, slot, asleep, true, &position))
        return false;
    button->bounds = position.bounds;
    return true;
}

void jelli_pet_discovery_update(JelliPetUi *ui, JelliGame *game, uint64_t now)
{
    if (jelli_invitations_update(game)) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
    }
    jelli_pet_discovery_refresh(ui, game, now);
}
