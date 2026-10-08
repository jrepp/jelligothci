#include "pet_draw.h"

static void ring_phase(const JelliPetUi *ui, JelliPetRenderKey *view, uint64_t elapsed,
                       uint64_t duration, bool target)
{
    bool swap = ui->ring_from_open && target;
    uint64_t phase_duration = swap ? duration / 2u : duration;
    if (phase_duration == 0u)
        return;
    bool leaving = ui->ring_from_open && (!target || elapsed < phase_duration);
    uint64_t phase_time = swap && !leaving ? elapsed - phase_duration : elapsed;
    unsigned linear = (unsigned)(phase_time * 255u / phase_duration);
    unsigned eased = 255u - (255u - linear) * (255u - linear) / 255u;
    view->ring_page = leaving ? ui->ring_from_page : (uint8_t)ui->page;
    view->ring_visible = (uint8_t)(leaving  ? ui->ring_from_visible * (255u - eased) / 255u
                                   : target ? eased
                                            : 0u);
    view->ring_moving = true;
}

static void ring_timing(JelliPetUi *ui, const JelliPet *pet, uint64_t time, JelliPetRenderKey *view)
{
    bool target = ui->menu_open && ui->page < JELLI_UI_BRUSH;
    bool changed = ui->rendered &&
                   (ui->menu_open != ui->last_view.menu_open || ui->page != ui->last_view.page);
    if (changed) {
        ui->ring_anchor_ms = time;
        ui->ring_started = true;
        ui->ring_from_page = ui->last_view.ring_page;
        ui->ring_from_visible = ui->last_view.ring_visible;
        ui->ring_from_open = ui->last_view.ring_visible > 0u;
    }
    uint32_t scale =
        jelli_tunable_get(&ui->tunables, pet->id, pet->form, JELLI_TUNE_ANIMATION_SCALE);
    uint64_t duration = 120u * scale / 100u;
    if (duration < 2u)
        duration = 2u;
    uint64_t elapsed = time >= ui->ring_anchor_ms ? time - ui->ring_anchor_ms : duration;
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
    /* Uneven holds and occasional posture changes; no frame-count RNG or catch-up. */
    static const uint8_t poses[] = {0, 1, 0, 0, 2, 0, 1, 1, 0, 3, 3, 0, 0, 1, 0, 0};
    uint64_t beat = (time - ui->idle_anchor_ms) / idle;
    unsigned pose = poses[beat % 16u];
    if (pose >= 2u && ((beat / 16u + pet->id) % 3u) == 0u)
        pose = 0u; /* Some cycles stay quiet instead of repeating every gesture. */
    view->phase = pet->activity == JELLI_IDLE && !pet->asleep ? pose : 0u;
    ring_timing(ui, pet, time, view);
    /* One stable mood tile; legacy carousel tunables remain wire-compatible. */
    view->stat_index = 0u;
    view->tile_phase = 0u;
}
