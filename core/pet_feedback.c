#include "pet_feedback.h"
#include "jelli/sound.h"

static bool pet_changed(const JelliPet *pet, JelliEventSnapshot before)
{
    if (pet->activity != before.activity || pet->health != before.health ||
        pet->bond != before.bond || pet->hydration != before.hydration ||
        pet->asleep != ((before.flags & 1u) != 0u) ||
        pet->reward_pending != ((before.flags & 2u) != 0u))
        return true;
    for (unsigned i = 0; i < JELLI_NEED_COUNT; ++i)
        if (pet->needs[i] != before.needs[i])
            return true;
    return false;
}

void jelli_pet_feedback(JelliPetUi *ui, const JelliGame *game, JelliEventSnapshot before,
                        uint32_t actor_id, bool back, int x, int y)
{
    if (ui->result != JELLI_OK)
        return;
    const JelliPet *pet = &game->pets[game->active];
    bool action = !back && pet->id == actor_id && pet_changed(pet, before);
    ui->sound_pending = true;
    ui->sound_cue = (uint8_t)((back     ? JELLI_SOUND_BACK
                               : action ? JELLI_SOUND_PET
                                        : JELLI_SOUND_CONFIRM) +
                              1u);
    if (!action)
        return;
    bool routine = ui->page >= JELLI_UI_BRUSH && ui->page <= JELLI_UI_STRETCH;
    if (routine && !ui->clicker_done && (ui->page == JELLI_UI_BRUSH || ui->page == JELLI_UI_WASH))
        return; /* These routines use localized bubbles instead of sprite sprays. */
    unsigned amount = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_BURST_COUNT);
    if (routine && ui->clicker_done && amount)
        amount = JELLI_PARTICLE_CAPACITY;
    unsigned spread = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_SPREAD);
    jelli_particles_burst_tuned(&ui->particles, x, y, true, amount, spread);
}

bool jelli_pet_ui_take_sound(JelliPetUi *ui, uint64_t now_ms)
{
    if (!ui || !ui->sound_pending)
        return false;
    ui->sound_pending = false;
    if (ui->sound_played && now_ms >= ui->last_sound_ms && now_ms - ui->last_sound_ms < 120u)
        return false;
    ui->sound_played = true;
    ui->last_sound_ms = now_ms;
    return true;
}

unsigned jelli_pet_ui_sound(JelliPetUi *ui, const JelliPet *pet, uint64_t now_ms)
{
    if (!ui || !pet)
        return 0u;
    unsigned cue = ui->sound_cue ? ui->sound_cue : JELLI_SOUND_CONFIRM + 1u;
    bool input = ui->sound_pending;
    bool tap = jelli_pet_ui_take_sound(ui, now_ms);
    bool enabled =
        jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_COO_ENABLED) != 0u;
    if (input || !enabled || ui->menu_open || pet->asleep || pet->activity != JELLI_IDLE ||
        ui->coo_pet != pet->id || now_ms < ui->coo_anchor_ms) {
        ui->coo_anchor_ms = now_ms;
        ui->coo_pet = pet->id;
        return tap ? cue : 0u;
    }
    unsigned interval = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_COO_MS);
    if (now_ms - ui->coo_anchor_ms < interval)
        return 0u;
    ui->coo_anchor_ms = now_ms; /* One cue after stalls; never catch up in bursts. */
    return 7u;
}
