#ifndef JELLI_ESP_SESSION_H
#define JELLI_ESP_SESSION_H

#include "jelli/pet_engine.h"
#include "jelli/save.h"
#include "driver/i2c_master.h"
#include "nvs.h"

typedef struct {
    JelliSave snapshot, decode_scratch;
    uint8_t bytes[JELLI_SAVE_CAPACITY];
    nvs_handle_t storage;
    i2c_master_dev_handle_t rtc;
    uint64_t sequence, last_save_ms, saved_ticks, sample_ms, sample_seconds, rtc_sample_ms;
    bool storage_ready, protected_save, rtc_initialized, clock_known, rtc_sampled, restored;
} JelliEspSession;

bool jelli_esp_session_checkpoint(JelliEspSession *session, JelliPetEngine *engine);
void jelli_esp_session_open(JelliEspSession *session, JelliPetEngine *engine);
void jelli_esp_session_update(JelliEspSession *session, JelliPetEngine *engine, bool frozen);

#endif
