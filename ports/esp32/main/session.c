#include "session.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* PCF85063A register layout: NXP data sheet, sections 7.2 and 7.3.
 * RTC is UTC; the persisted UI offset determines the displayed local time. */
static const char *TAG = "jelli_session";
_Static_assert(sizeof(JelliEspSession) <= 24576u, "ESP save workspace exceeds 24 KiB");

static unsigned month_days(unsigned year, unsigned month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1u] + (month == 2u && year % 4u == 0u ? 1u : 0u);
}

static bool bcd(uint8_t value, unsigned maximum, unsigned *out)
{
    unsigned low = value & 15u, high = value >> 4u;
    *out = high * 10u + low;
    return low <= 9u && high <= 9u && *out <= maximum;
}

static uint8_t to_bcd(unsigned value) { return (uint8_t)((value / 10u) * 16u + value % 10u); }

static bool rtc_read(JelliEspSession *session, uint64_t *seconds)
{
    uint8_t reg = 0u, data[11];
    if (!session->rtc || !session->rtc_initialized ||
        i2c_master_transmit_receive(session->rtc, &reg, 1u, data, sizeof(data), 10) != ESP_OK ||
        (data[0] & 0xa2u) || (data[4] & 0x80u))
        return false; /* STOP, external test, 12-hour mode, or oscillator-stop flag. */
    unsigned second, minute, hour, day, month, year;
    if (!bcd(data[4] & 0x7fu, 59u, &second) || !bcd(data[5] & 0x7fu, 59u, &minute) ||
        !bcd(data[6] & 0x3fu, 23u, &hour) || !bcd(data[7] & 0x3fu, 31u, &day) ||
        !bcd(data[9] & 0x1fu, 12u, &month) || !bcd(data[10], 99u, &year) || !day || !month)
        return false;
    year += 2000u;
    if (day > month_days(year, month))
        return false;
    uint64_t days = 10957u; /* Days from 1970-01-01 to 2000-01-01. */
    for (unsigned y = 2000u; y < year; ++y)
        days += y % 4u == 0u ? 366u : 365u;
    for (unsigned m = 1u; m < month; ++m)
        days += month_days(year, m);
    *seconds = (days + day - 1u) * 86400u + hour * 3600u + minute * 60u + second;
    return true;
}

static bool rtc_write(JelliEspSession *session, uint64_t seconds)
{
    if (!session->rtc || seconds < UINT64_C(946684800) || seconds >= UINT64_C(4102444800))
        return false;
    uint64_t days = seconds / 86400u - 10957u;
    unsigned year = 2000u, month = 1u;
    while (days >= (year % 4u == 0u ? 366u : 365u)) {
        days -= year % 4u == 0u ? 366u : 365u;
        ++year;
    }
    while (days >= month_days(year, month)) {
        days -= month_days(year, month);
        ++month;
    }
    uint8_t reg = 0u, control;
    if (i2c_master_transmit_receive(session->rtc, &reg, 1u, &control, 1u, 10) != ESP_OK)
        return false;
    uint8_t stop[] = {0u, (uint8_t)((control & 1u) | 0x20u)};
    uint8_t date[] = {4u,
                      to_bcd((unsigned)(seconds % 60u)),
                      to_bcd((unsigned)(seconds / 60u % 60u)),
                      to_bcd((unsigned)(seconds / 3600u % 24u)),
                      to_bcd((unsigned)days + 1u),
                      (uint8_t)((seconds / 86400u + 4u) % 7u),
                      to_bcd(month),
                      to_bcd(year - 2000u)};
    uint8_t start[] = {0u, (uint8_t)(control & 1u)};
    if (i2c_master_transmit(session->rtc, stop, sizeof(stop), 10) != ESP_OK)
        return false;
    esp_err_t written = i2c_master_transmit(session->rtc, date, sizeof(date), 10);
    esp_err_t started = i2c_master_transmit(session->rtc, start, sizeof(start), 10);
    return written == ESP_OK && started == ESP_OK;
}

static void clock_sample(JelliEspSession *session, JelliPetEngine *engine, uint64_t now)
{
    uint64_t interval = session->clock_known ? 60000u : 1000u;
    if (!session->rtc_sampled || now < session->rtc_sample_ms ||
        now - session->rtc_sample_ms >= interval) {
        uint64_t seconds;
        session->rtc_sample_ms = now;
        session->rtc_sampled = true;
        session->clock_known = rtc_read(session, &seconds);
        if (session->clock_known) {
            session->sample_ms = now;
            session->sample_seconds = seconds;
        }
    }
    bool known = session->clock_known && now >= session->sample_ms;
    uint64_t seconds = known ? session->sample_seconds + (now - session->sample_ms) / 1000u : 0u;
    engine->game.wall_known = known;
    engine->game.wall_seconds = seconds;
    engine->ui.clock_known = known;
    engine->ui.clock_minute = (uint16_t)(seconds / 60u % 1440u);
    engine->ui.time_unavailable = !known;
}

static bool checkpoint(JelliEspSession *session, JelliPetEngine *engine, uint64_t now)
{
    engine->ui.save_requested = false;
    session->last_save_ms = now;
    if (!session->storage_ready || session->protected_save || session->sequence == UINT64_MAX ||
        engine->game.resuming) {
        engine->ui.save_status = JELLI_SAVE_UNAVAILABLE;
        return false;
    }
    uint64_t elapsed = now >= engine->last_ms ? now - engine->last_ms : 0u;
    if (!engine->paused)
        jelli_game_advance(&engine->game, elapsed);
    for (unsigned n = 0u; n < 3u && engine->game.backlog_ms >= 100u; ++n)
        jelli_game_advance(&engine->game, 0u);
    engine->last_ms = now;
    engine->game.timezone_minutes = engine->ui.timezone_minutes;
    engine->game.clock_adjust = engine->ui.clock_adjust;
    session->snapshot = (JelliSave){.game = engine->game,
                                    .sequence = session->sequence + 1u,
                                    .anchor_valid = engine->game.wall_known,
                                    .anchor_ms = engine->game.wall_seconds * 1000u};
    size_t size = jelli_save_encode(&session->snapshot, session->bytes, sizeof(session->bytes));
    bool ok = size &&
              nvs_set_blob(session->storage, "checkpoint", session->bytes, size) == ESP_OK &&
              nvs_commit(session->storage) == ESP_OK;
    engine->ui.save_status = ok ? JELLI_SAVE_SAVED : JELLI_SAVE_FAILED;
    if (ok) {
        ++session->sequence;
        session->saved_ticks = engine->game.ticks;
    } else {
        ESP_LOGW(TAG, "Checkpoint failed; session remains unsaved");
    }
    return ok;
}

static void load_checkpoint(JelliEspSession *session, JelliPetEngine *engine)
{
    size_t size = 0u;
    esp_err_t result = nvs_get_blob(session->storage, "checkpoint", NULL, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND)
        return;
    if (result != ESP_OK || size > sizeof(session->bytes) ||
        nvs_get_blob(session->storage, "checkpoint", session->bytes, &size) != ESP_OK ||
        !jelli_save_decode_workspace(&session->snapshot, session->bytes, size,
                                     &session->decode_scratch)) {
        session->protected_save = true;
        ESP_LOGW(TAG, "Unreadable/unsupported checkpoint preserved; writes disabled");
        return;
    }
    engine->game = session->snapshot.game;
    session->sequence = session->snapshot.sequence;
    session->saved_ticks = engine->game.ticks;
    session->restored = true;
    engine->ui.timezone_minutes = engine->game.timezone_minutes;
    engine->ui.clock_adjust = engine->game.clock_adjust;
}

void jelli_esp_session_open(JelliEspSession *session, JelliPetEngine *engine)
{
    *session = (JelliEspSession){0};
    esp_err_t result = nvs_flash_init(); /* Never erase user data on initialization errors. */
    if (result == ESP_OK)
        result = nvs_open("jelli", NVS_READWRITE, &session->storage);
    session->storage_ready = result == ESP_OK;
    if (session->storage_ready) {
        uint8_t valid = 0u;
        session->rtc_initialized =
            nvs_get_u8(session->storage, "rtc_valid", &valid) == ESP_OK && valid == 1u;
        load_checkpoint(session, engine);
    } else {
        ESP_LOGW(TAG, "NVS unavailable: %s; existing partition preserved", esp_err_to_name(result));
    }
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x51u, .scl_speed_hz = 100000u};
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus && i2c_master_bus_add_device(bus, &config, &session->rtc) != ESP_OK)
        session->rtc = NULL;
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    clock_sample(session, engine, now);
    if (session->restored) {
        uint64_t wall = engine->game.wall_seconds * 1000u;
        bool trusted = engine->game.wall_known && session->snapshot.anchor_valid &&
                       wall >= session->snapshot.anchor_ms;
        jelli_game_resume_begin(&engine->game, trusted ? wall - session->snapshot.anchor_ms : 0u);
        for (unsigned n = 0u; n <= JELLI_OFFLINE_CAP_MS / 60000u && engine->game.resuming; ++n) {
            (void)jelli_game_resume_step(&engine->game);
            vTaskDelay(1); /* Startup catch-up yields between bounded one-minute segments. */
        }
        engine->last_ms = engine->platform.now_ms(engine->platform.ctx);
        if (!engine->game.resuming)
            (void)checkpoint(session, engine, engine->last_ms);
    }
    ESP_LOGI(TAG, "Session: save=%s RTC=%s",
             session->storage_ready && !session->protected_save ? "available" : "unavailable",
             session->clock_known ? "known" : "unknown (sync required)");
}

static void set_clock(JelliEspSession *session, JelliPetEngine *engine, uint64_t now)
{
    engine->ui.wall_set_requested = false;
    bool ok = session->storage_ready && !session->protected_save &&
              engine->ui.wall_set_seconds >= UINT64_C(946684800) &&
              engine->ui.wall_set_seconds < UINT64_C(4102444800) &&
              engine->ui.wall_set_offset_minutes >= -720 &&
              engine->ui.wall_set_offset_minutes <= 840;
    if (!ok) {
        engine->ui.result = JELLI_NOT_READY;
        return;
    }
    /* Revoke trust before changing RTC so a partial write cannot become valid after reboot. */
    ok = nvs_set_u8(session->storage, "rtc_valid", 0u) == ESP_OK &&
         nvs_commit(session->storage) == ESP_OK;
    session->rtc_initialized = false;
    if (ok)
        ok = rtc_write(session, engine->ui.wall_set_seconds);
    if (ok)
        ok = nvs_set_u8(session->storage, "rtc_valid", 1u) == ESP_OK &&
             nvs_commit(session->storage) == ESP_OK;
    session->rtc_initialized = ok;
    session->clock_known = false;
    session->rtc_sampled = false;
    if (ok) {
        engine->ui.timezone_minutes = engine->ui.wall_set_offset_minutes;
        engine->ui.clock_adjust = 0;
        engine->ui.save_requested = true;
    }
    clock_sample(session, engine, now);
    engine->ui.result = ok ? JELLI_OK : JELLI_NOT_READY;
}

void jelli_esp_session_update(JelliEspSession *session, JelliPetEngine *engine, bool frozen)
{
    if (frozen)
        return; /* Captures preserve paired pixels and game state. */
    uint64_t now = engine->platform.now_ms(engine->platform.ctx);
    if (engine->ui.wall_set_requested)
        set_clock(session, engine, now);
    clock_sample(session, engine, now);
    bool dirty = session->saved_ticks != engine->game.ticks;
    bool periodic = dirty && now >= session->last_save_ms && now - session->last_save_ms >= 60000u;
    if (!engine->game.resuming && (engine->ui.save_requested || periodic))
        (void)checkpoint(session, engine, now);
}

bool jelli_esp_session_checkpoint(JelliEspSession *session, JelliPetEngine *engine)
{
    return checkpoint(session, engine, engine->platform.now_ms(engine->platform.ctx));
}
