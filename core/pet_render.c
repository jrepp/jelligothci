#include "jelli/potty.h"
#include "jelli/creature.h"
#include "jelli/wake.h"
#include "pet_draw.h"
#include "pet_gallery.h"
#include "pet_collection.h"
#include <stddef.h>

static uint8_t recovery_seconds(const JelliPet *pet)
{
    if (pet->health != JELLI_RECOVERING || pet->interaction_due <= pet->ticks)
        return 0u;
    uint64_t ticks = pet->interaction_due - pet->ticks;
    return ticks >= 300u ? 30u : (uint8_t)((ticks + 9u) / 10u);
}

/* Touch reactions, or a wake mood after them; a happy wake opens with surprise. */
static void reaction_key(const JelliPet *pet, JelliPetRenderKey *key)
{
    key->reaction =
        pet->wake_mood ? (uint8_t)(JELLI_REACTION_TOUCH_OVERLOAD + pet->wake_mood) : pet->reaction;
    if (pet->wake_mood == JELLI_WAKE_HAPPY)
        key->phase =
            pet->reaction_ticks > jelli_wake_rules.reaction_ticks - jelli_wake_rules.surprise_ticks
                ? JELLI_POSE_CURIOUS
                : JELLI_POSE_IDLE; /* Surprise, then joy. */
}

static void activity_key(const JelliPet *pet, uint64_t time, JelliPetRenderKey *key)
{
    key->health = (uint8_t)pet->health;
    key->care_seconds = recovery_seconds(pet);
    key->activity = (uint8_t)pet->activity;
    key->care_blocked = (uint8_t)((jelli_pet_health_ready(pet, JELLI_HEALTH_MEDICINE) ? 0u : 1u) |
                                  (jelli_pet_health_ready(pet, JELLI_HEALTH_SHOT) ? 0u : 2u));
    key->moment = pet->moment;
    key->behavior = pet->behavior;
    unsigned frames = jelli_potty_rules.mess_sprite_count;
    if ((pet->behavior_flags & JELLI_PET_FLAG_MESS) && frames && jelli_potty_rules.mess_frame_ms)
        key->mess = (uint8_t)(1u + time / jelli_potty_rules.mess_frame_ms % frames);
    if (key->mess && pet->activity == JELLI_CLEANING) { /* Step from the clean's own progress. */
        uint64_t left = pet->interaction_due > pet->ticks ? pet->interaction_due - pet->ticks : 0u;
        uint64_t done = JELLI_CLEAN_TICKS - (left < JELLI_CLEAN_TICKS ? left : JELLI_CLEAN_TICKS);
        unsigned steps = jelli_potty_rules.sweep_steps;
        key->sweep = (uint8_t)(1u + done * (steps - 1u) / JELLI_CLEAN_TICKS);
    }
}

static JelliPetRenderKey render_key(const JelliGame *game, JelliPetUi *ui, uint64_t animation_ms,
                                    bool paused)
{
    const JelliPet *pet = &game->pets[game->active];
    uint8_t other_index = game->active == 0u ? 1u : 0u;
    const JelliPet *other = &game->pets[other_index];
    JelliPetRenderKey key = {.assets = ui->assets};
    uint64_t day_phase =
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) % JELLI_DAY_TICKS;
    jelli_pet_timing(ui, pet, animation_ms, &key);
    jelli_pet_gallery_key(ui, game, animation_ms, &key);
    jelli_pet_collection_key(ui, game, &key);
    if (paused) {
        /* Pausing the pet must not strand navigation behind a frozen transition. */
        key.ring_visible =
            ui->menu_open && ui->page < JELLI_UI_BRUSH && ui->page != JELLI_UI_COLLECTION ? 255u
                                                                                          : 0u;
        key.ring_page = (uint8_t)ui->page;
        key.ring_clock_edit = ui->clock_edit;
        key.ring_moving = false;
        ui->ring_started = false;
    }
    jelli_pet_atmosphere(ui, pet, animation_ms, &key);
    key.menu_open = ui->menu_open;
    if (ui->menu_open) {
        for (unsigned slot = 1; slot <= JELLI_PRIZE_COUNT; ++slot)
            if (jelli_pet_ui_available(ui, game, slot) != JELLI_OK)
                key.unavailable |= (uint16_t)(1u << slot);
    }
    if (!ui->menu_open && ui->latched_prize && !game->prizes.offered &&
        jelli_pet_gallery_available(ui, game, 1u) != JELLI_OK)
        key.unavailable |= 2u;
    key.clicker_hits = ui->clicker_hits;
    key.clicker_goal = ui->clicker_goal;
    key.clicker_stage = ui->clicker_stage;
    key.clicker_done = ui->clicker_done;
    key.clock_known = ui->clock_known;
    key.clock_minute = jelli_pet_clock_minute(ui, pet);
    key.clock_edit = ui->clock_edit;
    key.timezone_minutes = ui->timezone_minutes;
    key.minute = (uint32_t)(day_phase / 600u);
    key.day =
        pet->ticks / JELLI_DAY_TICKS +
        (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) / JELLI_DAY_TICKS;
    key.active_id = pet->id;
    key.stored_id = other->id;
    key.result = ui->result;
    key.attempted_slot = ui->attempted_slot;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        key.needs[i] = pet->needs[i];
    key.bond = pet->bond;
    key.hydration = pet->hydration;
    key.volume = game->volume;
    key.mood = (uint8_t)jelli_pet_mood(pet);
    reaction_key(pet, &key);
    key.food = game->food;
    key.gifts = game->gifts;
    key.active = game->active;
    key.count = game->count;
    key.page = (uint8_t)ui->page;
    key.save_status = ui->save_status;
    key.form = pet->form;
    key.location = pet->location;
    activity_key(pet, animation_ms, &key);
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
    return a->unavailable == b->unavailable && a->ring_page == b->ring_page &&
           a->ring_visible == b->ring_visible && a->clock_edit == b->clock_edit &&
           a->ring_clock_edit == b->ring_clock_edit && a->timezone_minutes == b->timezone_minutes &&
           a->ring_moving == b->ring_moving && a->care_blocked == b->care_blocked;
}

static bool same_tile_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->stat_index == b->stat_index && a->stat_value == b->stat_value &&
           a->sleep_score == b->sleep_score && a->reward_index == b->reward_index &&
           a->reward_active == b->reward_active && a->tile_phase == b->tile_phase;
}

static bool same_actor_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->phase == b->phase && a->pose == b->pose && a->clip_frame == b->clip_frame &&
           a->moment == b->moment && a->behavior == b->behavior && a->mess == b->mess &&
           a->sweep == b->sweep;
}

static bool same_frame_key(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return same_ring_key(a, b) && same_tile_key(a, b) && same_activity_key(a, b) &&
           a->night == b->night && a->clock_known == b->clock_known &&
           a->clock_minute == b->clock_minute && a->menu_open == b->menu_open &&
           same_actor_key(a, b) && a->minute == b->minute && a->day == b->day &&
           a->active == b->active && a->count == b->count && a->page == b->page &&
           a->result == b->result && a->attempted_slot == b->attempted_slot &&
           a->save_status == b->save_status && a->time_unavailable == b->time_unavailable &&
           a->paused == b->paused && a->resuming == b->resuming;
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
    return a->care_seconds == b->care_seconds && a->volume == b->volume &&
           a->hydration == b->hydration && a->assets == b->assets && same_frame_key(a, b) &&
           same_pet_key(a, b) && jelli_pet_gallery_same(a, b) && jelli_pet_collection_same(a, b);
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
    jelli_pet_actor_clip(ui, &view, time);
    jelli_pet_actor_layout(ui, &view);
    jelli_pet_sleep_particles(ui, view.asleep, time);
    jelli_pet_bubbles(ui, view.asleep, time);
    if (ui->rendered && same_render_key(&view, &ui->last_view)) {
        surface->damage = (JelliRect){0};
        jelli_pet_draw_particles(surface, game, ui);
        return;
    }
    JelliPetRenderKey previous = ui->last_view;
    previous.mood = view.mood;
    previous.stat_index = view.stat_index;
    previous.tile_phase = view.tile_phase;
    previous.stat_value = view.stat_value;
    previous.sleep_score = view.sleep_score;
    previous.reward_index = view.reward_index;
    previous.reward_active = view.reward_active;
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
