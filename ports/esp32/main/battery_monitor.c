#include "battery_monitor.h"
#include "jelli/drivers/axp2101.h"
#include "jelli/power_log.h"
#include "power_history.h"
#include "register_io.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Queues coalesce intermediate updates. The worker owns the PMIC and history;
 * the engine receives only the latest sample and never waits for I2C or NVS. */
typedef struct {
    JelliAxp2101Sample sample;
    bool available;
} Reading;
typedef struct {
    uint64_t wall_seconds;
    bool wall_valid, display_blank, pet_asleep;
} Context;
static QueueHandle_t readings, contexts;
static i2c_master_dev_handle_t device;
static uint64_t last_context_ms;
static Context sent_context;
static bool context_sent;

static uint64_t monotonic_ms(void) { return (uint64_t)esp_timer_get_time() / 1000u; }

static JelliPowerRecord make_record(const Reading *reading, const Context *context, uint32_t boot,
                                    uint8_t reasons)
{
    JelliPowerRecord result = {.boot = boot,
                               .monotonic_ms = monotonic_ms(),
                               .wall_seconds = context->wall_valid ? context->wall_seconds : 0u,
                               .battery_mv = reading->sample.battery_mv,
                               .battery_percent = reading->sample.gauge_percent,
                               .status = {reading->sample.status[0], reading->sample.status[1]},
                               .reasons = reasons,
                               .reset_reason = (uint8_t)esp_reset_reason()};
    if (context->wall_valid)
        result.flags |= JELLI_POWER_WALL_VALID;
    if (context->display_blank)
        result.flags |= JELLI_POWER_DISPLAY_BLANK;
    if (context->pet_asleep)
        result.flags |= JELLI_POWER_PET_ASLEEP;
    if (reading->available) {
        result.flags |= JELLI_POWER_READING_VALID;
        if (reading->sample.battery_present)
            result.flags |= JELLI_POWER_BATTERY_PRESENT;
        if (reading->sample.voltage_valid)
            result.flags |= JELLI_POWER_VOLTAGE_VALID;
        if (reading->sample.gauge_valid)
            result.flags |= JELLI_POWER_GAUGE_VALID;
    }
    return result;
}

static void sample_task(void *ctx)
{
    (void)ctx;
    JelliRegisterIo io = jelli_esp_register_io(device);
    Reading reading = {0};
    Context context = {0};
    uint64_t next_sample = 0u, last_write = 0u;
    uint32_t boot = esp_random();
    uint8_t pending = JELLI_POWER_BOOT;
    bool have_context = false;
    jelli_power_history_init();
    for (;;) {
        Context next;
        if (xQueueReceive(contexts, &next, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (have_context && next.display_blank != context.display_blank)
                pending |= JELLI_POWER_DISPLAY_CHANGE;
            if (have_context && next.pet_asleep != context.pet_asleep)
                pending |= JELLI_POWER_PET_SLEEP_CHANGE;
            if (next.display_blank != context.display_blank)
                next_sample = 0u;
            context = next;
            have_context = true;
        }
        uint64_t now = monotonic_ms();
        if (now >= next_sample) {
            Reading next_reading = {0};
            next_reading.available =
                jelli_axp2101_sample(io, &next_reading.sample) == JELLI_DEVICE_OK;
            if (reading.available != next_reading.available ||
                (reading.available && (reading.sample.status[0] != next_reading.sample.status[0] ||
                                       reading.sample.status[1] != next_reading.sample.status[1])))
                pending |= JELLI_POWER_SOURCE_CHANGE;
            reading = next_reading;
            (void)xQueueOverwrite(readings, &reading);
            next_sample = now + (context.display_blank ? 1000u : 30000u);
        }
        if (last_write && now - last_write >= 1200000u)
            pending |= JELLI_POWER_PERIODIC;
        /* Coalesce rapid changes and bound flash writes to at most two/minute. */
        if (have_context && pending && (!last_write || now - last_write >= 30000u)) {
            jelli_power_history_append(make_record(&reading, &context, boot, pending));
            ESP_LOGI("battery", "Worker stack free: %u bytes",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            pending = 0u;
            last_write = now;
        }
    }
}

bool jelli_battery_monitor_start(i2c_master_bus_handle_t bus)
{
    if (!bus || readings)
        return false;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34, .scl_speed_hz = 100000};
    if (i2c_master_bus_add_device(bus, &config, &device) != ESP_OK)
        return false;
    readings = xQueueCreate(1, sizeof(Reading));
    contexts = xQueueCreate(1, sizeof(Context));
    if (!readings || !contexts ||
        xTaskCreate(sample_task, "battery", 4096, NULL, 2, NULL) != pdPASS) {
        if (readings)
            vQueueDelete(readings);
        if (contexts)
            vQueueDelete(contexts);
        readings = NULL;
        contexts = NULL;
        (void)i2c_master_bus_rm_device(device);
        device = NULL;
        return false;
    }
    return true;
}

void jelli_battery_monitor_poll(JelliPetEngine *engine, bool display_blank, uint64_t now)
{
    if (!engine || !readings)
        return;
    Reading reading;
    if (xQueueReceive(readings, &reading, 0) == pdTRUE) {
        engine->ui.battery_reading_valid = reading.available;
        engine->ui.battery_status[0] = reading.sample.status[0];
        engine->ui.battery_status[1] = reading.sample.status[1];
        engine->ui.battery_present = reading.available && reading.sample.battery_present;
        engine->ui.battery_voltage_valid = reading.available && reading.sample.voltage_valid;
        engine->ui.battery_gauge_valid = reading.available && reading.sample.gauge_valid;
        engine->ui.battery_mv = reading.sample.battery_mv;
        engine->ui.battery_percent = reading.sample.gauge_percent;
    }
    Context context = {.wall_seconds = engine->game.wall_seconds,
                       .wall_valid = engine->game.wall_known,
                       .display_blank = display_blank,
                       .pet_asleep = engine->game.pets[engine->game.active].asleep};
    if (!context_sent || context.display_blank != sent_context.display_blank ||
        context.pet_asleep != sent_context.pet_asleep ||
        context.wall_valid != sent_context.wall_valid || now < last_context_ms ||
        now - last_context_ms >= 1000u) {
        (void)xQueueOverwrite(contexts, &context);
        sent_context = context;
        context_sent = true;
        last_context_ms = now;
    }
}
