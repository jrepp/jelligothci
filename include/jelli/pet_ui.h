#ifndef JELLI_PET_UI_H
#define JELLI_PET_UI_H

#include "jelli/engine.h"
#include "jelli/assets.h"
#include "jelli/game.h"
#include "jelli/particles.h"
#include "jelli/tunables.h"
#include "jelli/pet_rewards.h"
#include <stdbool.h>
#include <stdint.h>

#define JELLI_ACTIVITY_TAPS 3u

typedef enum {
    JELLI_UI_HOME,
    JELLI_UI_CARE,
    JELLI_UI_MORE,
    JELLI_UI_COLLECTION,
    JELLI_UI_SETTINGS,
    JELLI_UI_MOMENTS,
    JELLI_UI_HEALTH,
    JELLI_UI_BRUSH,
    JELLI_UI_MEDICINE,
    JELLI_UI_SHOT,
    JELLI_UI_WASH,
    JELLI_UI_STRETCH,
    JELLI_UI_PETS,
    JELLI_UI_PET_DETAIL,
    JELLI_UI_EVOLUTIONS,
    JELLI_UI_PRESENT_ACTION,
    JELLI_UI_FOOD,
    JELLI_UI_PAGE_COUNT
} JelliPetPage;

typedef enum {
    JELLI_SAVE_UNAVAILABLE,
    JELLI_SAVE_PENDING,
    JELLI_SAVE_SAVED,
    JELLI_SAVE_FAILED
} JelliSaveStatus;

typedef enum {
    JELLI_UI_ACTION_FEED,
    JELLI_UI_ACTION_CARE,
    JELLI_UI_ACTION_REST_WAKE,
    JELLI_UI_ACTION_COLLECTION,
    JELLI_UI_ACTION_MORE,
    JELLI_UI_ACTION_SETTINGS,
    JELLI_UI_ACTION_BASIC_CARE,
    JELLI_UI_ACTION_PLAY,
    JELLI_UI_ACTION_CLEAN_WAKE,
    JELLI_UI_ACTION_HOME,
    JELLI_UI_ACTION_GIFT,
    JELLI_UI_ACTION_CLAIM,
    JELLI_UI_ACTION_TRAVEL,
    JELLI_UI_ACTION_SWITCH_PET,
    JELLI_UI_ACTION_BEDTIME,
    JELLI_UI_ACTION_SAVE,
    JELLI_UI_ACTION_MOMENTS,
    JELLI_UI_ACTION_BREAKFAST,
    JELLI_UI_ACTION_TEA,
    JELLI_UI_ACTION_OUTING,
    JELLI_UI_ACTION_MOVIE,
    JELLI_UI_ACTION_SUGGEST,
    JELLI_UI_ACTION_HEALTH,
    JELLI_UI_ACTION_BRUSH,
    JELLI_UI_ACTION_MEDICINE,
    JELLI_UI_ACTION_SHOT,
    JELLI_UI_ACTION_WASH,
    JELLI_UI_ACTION_STRETCH,
    JELLI_UI_ACTION_WATER,
    JELLI_UI_ACTION_EXERCISE,
    JELLI_UI_ACTION_VOLUME_DOWN,
    JELLI_UI_ACTION_VOLUME_UP
} JelliPetUiAction;

typedef struct {
    const char *label;
    JelliPetUiAction action;
} JelliPetUiItem;

typedef struct {
    JelliRect bounds;
    const char *label;
    uint32_t icon;
    unsigned scale;
    bool circular;
} JelliPetUiButton;

typedef struct {
    const JelliAssetSet *assets;
    uint32_t phase;
    uint8_t night, mood, reaction, care_blocked, ring_page, ring_visible;
    bool ring_moving, clock_edit, ring_clock_edit;
    uint16_t unavailable;
    uint16_t stat_value, sleep_score, prize_owned, prize_discovered;
    uint32_t prize_origins[JELLI_PRIZE_COUNT];
    uint8_t reward_index, offered_prize, latched_prize, highlighted_prize;
    uint8_t catch_phase;
    uint8_t selected_pet, selected_form, active_entry;
    uint8_t pet_forms[9], pet_reached[9];
    uint16_t pets_owned, pets_new;
    bool reward_active;
    int16_t timezone_minutes;
    uint16_t tile_phase;
    uint8_t stat_index;
    uint8_t clicker_hits, clicker_goal, clicker_stage;
    bool clicker_done;
    bool menu_open, clock_known;
    uint16_t clock_minute;
    uint32_t minute;
    uint32_t active_id;
    uint32_t stored_id;
    uint64_t day;
    JelliResult result;
    uint8_t attempted_slot;
    uint16_t needs[JELLI_NEED_COUNT];
    uint16_t bond, hydration;
    uint8_t volume;
    uint16_t food;
    uint16_t gifts;
    uint8_t active;
    uint8_t count;
    uint8_t page;
    uint8_t save_status;
    uint8_t form;
    uint8_t pose, clip_frame; /* JelliCreaturePose and its clip frame index. */
    uint8_t location;
    uint8_t health, care_seconds;
    uint8_t activity;
    uint8_t stored_form;
    uint8_t bedtime;
    bool asleep;
    bool stored_asleep;
    bool reward_pending;
    bool reward_claimed;
    bool time_unavailable;
    bool paused;
    bool resuming;
} JelliPetRenderKey;

typedef struct {
    const JelliAssetSet *assets; /* Host-owned override; NULL uses embedded art. */
    /* Borrowed immutable metadata; no runtime alpha scans or allocation. */
    const JelliAsset *actor_frame;
    JelliPetRewards rewards;
    uint64_t catch_anchor_ms;
    uint8_t latched_prize, highlighted_prize, catch_seen;
    uint8_t selected_pet, selected_form;
    uint8_t sound_cue; /* One-based cue; zero defaults to menu confirmation. */
    JelliPetPage present_return;
    JelliGame action_scratch; /* Fixed preflight workspace; never placed on the ESP task stack. */
    uint64_t tile_anchor_ms, idle_anchor_ms, last_animation_phase, last_pet_ticks;
    uint64_t last_sound_ms, coo_anchor_ms, night_anchor_ms, sleep_emit_ms, ring_anchor_ms;
    JelliPetRenderKey last_view;
    JelliTunables tunables;
    JelliPetPage page;
    int actor_x, actor_y;
    uint32_t clicker_pet, routine_random, tuning_revision, tuning_pet;
    JelliResult result;
    uint8_t attempted_slot;
    uint32_t result_until_ms, last_revision, coo_pet;
    JelliRect actor_bounds;
    JelliParticles particles;
    uint64_t wall_set_seconds;
    int16_t wall_set_offset_minutes;
    bool wall_set_requested;
    uint16_t clock_minute;
    uint8_t clicker_hits, clicker_goal, clicker_stage;
    bool clicker_done;
    uint8_t tuning_form;
    uint64_t clip_anchor_ms;
    uint32_t clip_pet;
    uint8_t clip_pose, clip_form;
    bool clip_started;
    bool tile_reset;
    uint8_t last_page, ring_from_page, ring_from_visible;
    bool ring_from_open, ring_started, clock_edit, ring_from_clock_edit;
    int16_t timezone_minutes, clock_adjust;
    bool save_requested, time_unavailable;
    uint8_t save_status;
    bool rendered, menu_open, clock_known;
    uint8_t stat_offset;
    bool sound_pending, sound_played;
    uint8_t night_from, night_target;
    bool atmosphere_ready, sleep_emitted;
    uint64_t bubble_emit_ms;
    uint8_t bubble_mode;
    bool bubble_emitted;
    uint8_t routine_goals[6];
} JelliPetUi;

bool jelli_pet_touch_actor(JelliPetUi *ui, JelliGame *game, int x, int y);
void jelli_pet_ui_init(JelliPetUi *ui);
JelliPetUiItem jelli_pet_ui_item(JelliPetPage page, unsigned item, bool asleep);
bool jelli_pet_ui_button(JelliPetPage page, unsigned slot, bool asleep, bool menu_open,
                         JelliPetUiButton *button);
bool jelli_pet_ring_button(const JelliPetUi *ui, unsigned slot, bool asleep,
                           JelliPetUiButton *button);
bool jelli_pet_ui_starts(unsigned page, unsigned slot);
bool jelli_pet_ui_control(const JelliPetUi *ui, unsigned slot, bool asleep,
                          JelliPetUiButton *button);
bool jelli_pet_health_select(JelliPetUi *ui, const JelliGame *game, JelliPetUiAction action);
unsigned jelli_pet_health_action(const JelliPetUi *ui);
void jelli_pet_health_tap(JelliPetUi *ui, JelliGame *game);
void jelli_pet_ui_back(JelliPetUi *ui);
/* Consume one cosmetic cue; rapid taps coalesce and never delay input. */
bool jelli_pet_ui_take_sound(JelliPetUi *ui, uint64_t now_ms);
/* Cue ID + 1, zero for silence. Call once per unfrozen engine frame. */
unsigned jelli_pet_ui_sound(JelliPetUi *ui, const JelliPet *pet, uint64_t now_ms);
unsigned jelli_pet_stat_score(uint16_t value);
unsigned jelli_pet_moment(const JelliPet *pet);
unsigned jelli_pet_suggested_moment(const JelliPet *pet, const JelliPetUi *ui);
bool jelli_pet_ui_command(const JelliPetUi *ui, const JelliGame *game, JelliPetUiAction action,
                          JelliCommand *command);
JelliResult jelli_pet_ui_available(JelliPetUi *ui, const JelliGame *game, unsigned slot);
uint16_t jelli_pet_clock_minute(const JelliPetUi *ui, const JelliPet *pet);
bool jelli_pet_clock_button(bool editing, unsigned slot, JelliPetUiButton *button);
void jelli_pet_clock_action(JelliPetUi *ui, unsigned slot);
void jelli_pet_ui_swipe(JelliPetUi *ui, JelliGame *game, int dx, int dy);
void jelli_pet_ui_tap(JelliPetUi *ui, JelliGame *game, int x, int y);
void jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                      uint64_t animation_ms, bool paused);

#endif
