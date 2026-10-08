#ifndef JELLI_PET_UI_H
#define JELLI_PET_UI_H

#include "jelli/engine.h"
#include "jelli/game.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    JELLI_UI_HOME,
    JELLI_UI_CARE,
    JELLI_UI_MORE,
    JELLI_UI_COLLECTION,
    JELLI_UI_SETTINGS
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
    JELLI_UI_ACTION_SAVE
} JelliPetUiAction;

typedef struct {
    const char *label;
    JelliPetUiAction action;
} JelliPetUiItem;

typedef struct {
    uint32_t phase;
    uint32_t minute;
    uint32_t active_id;
    uint32_t stored_id;
    uint64_t day;
    JelliResult result;
    uint16_t needs[JELLI_NEED_COUNT];
    uint16_t bond;
    uint16_t food;
    uint16_t gifts;
    uint8_t active;
    uint8_t count;
    uint8_t page;
    uint8_t save_status;
    uint8_t form;
    uint8_t location;
    uint8_t health;
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
    JelliPetPage page;
    JelliResult result;
    uint32_t result_until_ms;
    uint32_t last_animation_phase;
    JelliPetRenderKey last_view;
    uint64_t last_pet_ticks;
    uint32_t last_revision;
    uint8_t last_page;
    bool save_requested;
    bool time_unavailable;
    uint8_t save_status;
    bool rendered;
} JelliPetUi;

void jelli_pet_ui_init(JelliPetUi *ui);
JelliPetUiItem jelli_pet_ui_item(JelliPetPage page, unsigned item, bool asleep);
void jelli_pet_ui_tap(JelliPetUi *ui, JelliGame *game, int x, int y);
void jelli_pet_render(JelliSurface *surface, const JelliGame *game, JelliPetUi *ui,
                      uint32_t animation_ms, bool paused);

#endif
