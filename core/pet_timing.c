#include "pet_draw.h"

static void ring_phase(const JelliPetUi *ui, JelliPetRenderKey *view, uint64_t elapsed,
                       uint64_t duration, bool target)
{
    bool swap = ui->ring_from_open && target &&
                (ui->ring_from_page != ui->page || ui->ring_from_clock_edit != ui->clock_edit);
    uint64_t phase_duration = swap ? duration / 2u : duration;
    if (phase_duration == 0u)
        return;
    bool leaving = ui->ring_from_open && (!target || (swap && elapsed < phase_duration));
    uint64_t phase_time = swap && !leaving ? elapsed - phase_duration : elapsed;
    unsigned linear = (unsigned)(phase_time * 255u / phase_duration);
    /* Brief ease-in (first 1/16), then a quartic tail: ~75% travel by 30% time.
     * Integer arithmetic keeps the final pixel holds stable on the native panel. */
    unsigned progress = linear < 16u ? linear * linear / 32u : linear - 8u;
    unsigned remaining = 255u - progress * 255u / 247u;
    unsigned square = remaining * remaining / 255u;
    unsigned eased = 255u - square * square / 255u;
    view->ring_clock_edit = leaving ? ui->ring_from_clock_edit : ui->clock_edit;
    view->ring_page = leaving ? ui->ring_from_page : (uint8_t)ui->page;
    unsigned start = swap ? 0u : ui->ring_from_visible;
    view->ring_visible = (uint8_t)(leaving  ? ui->ring_from_visible * (255u - eased) / 255u
                                   : target ? start + (255u - start) * eased / 255u
                                            : 0u);
    view->ring_moving = true;
}

static void ring_timing(JelliPetUi *ui, const JelliPet *pet, uint64_t time, JelliPetRenderKey *view)
{
    bool target = ui->menu_open && ui->page < JELLI_UI_BRUSH && ui->page != JELLI_UI_COLLECTION;
    bool changed = ui->rendered &&
                   (ui->menu_open != ui->last_view.menu_open || ui->page != ui->last_view.page ||
                    ui->clock_edit != ui->last_view.clock_edit);
    if (changed) {
        ui->ring_anchor_ms = time;
        ui->ring_started = true;
        ui->ring_from_page = ui->last_view.ring_page;
        ui->ring_from_clock_edit = ui->last_view.ring_clock_edit;
        ui->ring_from_visible = ui->last_view.ring_visible;
        ui->ring_from_open = ui->last_view.ring_visible > 0u;
    }
    uint32_t scale =
        jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_ANIMATION_SCALE);
    uint64_t duration = 120u * scale / 100u;
    if (duration < 2u)
        duration = 2u;
    uint64_t elapsed = time >= ui->ring_anchor_ms ? time - ui->ring_anchor_ms : duration;
    view->ring_clock_edit = ui->clock_edit;
    view->ring_page = (uint8_t)ui->page;
    view->ring_visible = target ? 255u : 0u;
    if (!ui->rendered || !ui->ring_started || elapsed >= duration) {
        ui->ring_started = false;
        return;
    }
    ring_phase(ui, view, elapsed, duration, target);
}

void jelli_pet_timing(JelliPetUi *ui, const JelliPet *pet, uint64_t time, JelliPetRenderKey *view)
{
    bool changed = ui->tuning_revision != ui->tunables.revision ||
                   (ui->rendered && (ui->tuning_pet != pet->id || ui->tuning_form != pet->form)) ||
                   time < ui->last_animation_phase;
    if (changed) {
        ui->idle_anchor_ms = time;
        if (!ui->tile_reset)
            ui->stat_offset = ui->last_view.stat_index;
    }
    if (changed || ui->tile_reset) {
        ui->tile_anchor_ms = time;
        ui->tile_reset = false;
    }
    ui->tuning_revision = ui->tunables.revision;
    ui->tuning_pet = pet->id;
    ui->tuning_form = pet->form;
    uint32_t idle = jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_IDLE_MS);
    uint32_t scale =
        jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_ANIMATION_SCALE);
    /* Uneven holds and occasional posture changes; no frame-count RNG or catch-up. */
    static const uint8_t poses[] = {0, 1, 0, 0, 2, 0, 1, 1, 0, 3, 3, 0, 0, 1, 0, 0};
    uint64_t beat = (time - ui->idle_anchor_ms) / idle;
    unsigned pose = poses[beat % 16u];
    if (pose >= 2u && ((beat / 16u + pet->id) % 3u) == 0u)
        pose = 0u; /* Some cycles stay quiet instead of repeating every gesture. */
    view->phase = pet->activity == JELLI_IDLE && !pet->asleep ? pose : 0u;
    if (pet->activity == JELLI_EXERCISING)
        view->phase = (unsigned)(time / 500u % 2u);
    ring_timing(ui, pet, time, view);
    /* Manual pages only: never slide or wrap a tile automatically. */
    view->stat_index = (uint8_t)(ui->stat_offset % JELLI_PET_STAT_COUNT);
    view->stat_value = jelli_pet_reward_stat(pet, view->stat_index);
    view->sleep_score = jelli_habits_sleep_score(&pet->habits);
    view->reward_active =
        jelli_pet_rewards_animate(&ui->rewards, &ui->particles, time, 300u * scale / 100u,
                                  !ui->menu_open, &ui->stat_offset, &view->stat_value);
    view->stat_index = ui->stat_offset;
    view->reward_index = ui->rewards.index;
    view->tile_phase = 0u;
}
