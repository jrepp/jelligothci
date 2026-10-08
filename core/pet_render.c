#include "pet_draw.h"
#include <stddef.h>

static JelliPetRenderKey render_key(const JelliGame *game, JelliPetUi *ui, uint64_t animation_ms,
                                    bool paused)
{
    const JelliPet *pet = &game->pets[game->active];
    uint8_t other_index = game->active == 0u ? 1u : 0u;
    const JelliPet *other = &game->pets[other_index];
    JelliPetRenderKey key = {0};
    uint64_t day_phase =
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) % JELLI_DAY_TICKS;
    jelli_pet_timing(ui, pet, animation_ms, &key);
    jelli_pet_atmosphere(ui, pet, animation_ms, &key);
    key.menu_open = ui->menu_open;
    key.clicker_hits = ui->clicker_hits;
    key.clicker_goal = ui->clicker_goal;
    key.clicker_stage = ui->clicker_stage;
    key.clicker_done = ui->clicker_done;
    key.clock_known = ui->clock_known;
    key.clock_minute = ui->clock_minute;
    key.minute = (uint32_t)(day_phase / 600u);
    key.day =
        pet->ticks / JELLI_DAY_TICKS +
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) / JELLI_DAY_TICKS;
    key.active_id = pet->id;
    key.stored_id = other->id;
    key.result = ui->result;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        key.needs[i] = pet->needs[i];
    key.bond = pet->bond;
    key.mood = (uint8_t)jelli_pet_mood(pet);
    key.reaction = pet->reaction;
    key.care_blocked = (uint8_t)((jelli_pet_health_ready(pet, 1u) ? 0u : 1u) |
                                 (jelli_pet_health_ready(pet, 2u) ? 0u : 2u));
    key.food = game->food;
    key.gifts = game->gifts;
    key.active = game->active;
    key.count = game->count;
    key.page = (uint8_t)ui->page;
    key.save_status = ui->save_status;
    key.form = pet->form;
    key.location = pet->location;
    key.health = (uint8_t)pet->health;
    key.activity = (uint8_t)pet->activity;
    key.stored_form = other->form;
    key.bedtime = (uint8_t)pet->bedtime;
    key.asleep = pet->asleep;
    key.stored_asleep = other->asleep;
    key.reward_pending = pet->reward_pending;
    key.reward_claimed = pet->reward_claimed;
    key.time_unavailable = ui->time_unavailable;
    key.paused = paused;
    key.resuming = game->resuming;
    return key;
}

static bool same_activity_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->clicker_hits == b->clicker_hits && a->clicker_goal == b->clicker_goal &&
           a->clicker_stage == b->clicker_stage && a->clicker_done == b->clicker_done;
}

static bool same_ring_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->ring_page == b->ring_page && a->ring_visible == b->ring_visible &&
           a->ring_moving == b->ring_moving && a->care_blocked == b->care_blocked;
}

static bool same_frame_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return same_ring_key(a, b) && a->night == b->night && same_activity_key(a, b) &&
           a->clock_known == b->clock_known && a->clock_minute == b->clock_minute &&
           a->menu_open == b->menu_open && a->stat_index == b->stat_index &&
           a->tile_phase == b->tile_phase && a->phase == b->phase && a->minute == b->minute &&
           a->day == b->day && a->active == b->active && a->count == b->count &&
           a->page == b->page && a->result == b->result && a->save_status == b->save_status &&
           a->time_unavailable == b->time_unavailable && a->paused == b->paused &&
           a->resuming == b->resuming;
}

static bool same_pet_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    if (a->mood != b->mood || a->reaction != b->reaction || a->active_id != b->active_id ||
        a->stored_id != b->stored_id || a->bond != b->bond || a->food != b->food ||
        a->gifts != b->gifts || a->form != b->form || a->location != b->location ||
        a->health != b->health || a->activity != b->activity || a->stored_form != b->stored_form ||
        a->bedtime != b->bedtime || a->asleep != b->asleep ||
        a->stored_asleep != b->stored_asleep || a->reward_pending != b->reward_pending ||
        a->reward_claimed != b->reward_claimed)
        return false;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        if (a->needs[i] != b->needs[i])
            return false;
    }
    return true;
}

static bool same_render_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return same_frame_key(a, b) && same_pet_key(a, b);
}

void jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                      uint64_t animation_ms, bool paused)
{
    if (surface == NULL || game == NULL || ui == NULL || surface->pixels == NULL ||
        surface->width < JELLI_WIDTH || surface->height < JELLI_HEIGHT ||
        surface->stride < surface->width || !jelli_game_valid(game))
        return;
    uint64_t time = paused && ui->rendered ? ui->last_animation_phase : animation_ms;
    JelliPetRenderKey view = render_key(game, ui, time, paused);
    jelli_pet_actor_layout(ui, &view);
    jelli_pet_sleep_particles(ui, view.asleep, time);
    if (ui->rendered && same_render_key(&view, &ui->last_view)) {
        surface->damage = (JelliRect){0};
        jelli_pet_draw_particles(surface, game, ui);
        return;
    }
    JelliPetRenderKey previous = ui->last_view;
    previous.mood = view.mood;
    previous.stat_index = view.stat_index;
    previous.tile_phase = view.tile_phase;
    for (unsigned i = 0; i < JELLI_NEED_COUNT; ++i)
        previous.needs[i] = view.needs[i];
    bool tile_only = ui->rendered && !ui->menu_open && same_render_key(&view, &previous);
    ui->last_view = view;
    jelli_pet_actor_layout(ui, &view);
    if (tile_only) {
        surface->damage = (JelliRect){90u, 282u, 286u, 100u};
        jelli_pet_draw_tile(surface, &view);
    } else {
        surface->damage = (JelliRect){0u, 0u, JELLI_WIDTH, JELLI_HEIGHT};
        jelli_pet_draw(surface, game, ui, &view);
    }
    ui->last_animation_phase = time;
    ui->last_pet_ticks = game->pets[game->active].ticks;
    ui->last_revision = game->revision;
    ui->last_page = (uint8_t)ui->page;
    ui->rendered = true;
    jelli_pet_draw_particles(surface, game, ui);
}
