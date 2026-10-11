#include "qmi_probe.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* QST QMI8658C Rev A, document 13-52-27: sections 5.2, 5.10 and 12.
 * Register reads are single-byte because reset CTRL1 disables auto-increment.
 * No reset, calibration, FIFO, gyro or power-rail commands are issued. */
static i2c_master_dev_handle_t device;
static const uint8_t registers[JELLI_QMI_REG_COUNT] = {2, 3, 4, 6, 8, 9, 10, 11, 12, 20};
static const uint8_t baseline[JELLI_QMI_REG_COUNT] = {0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static esp_err_t read_byte(uint8_t reg, uint8_t *value)
{
    return device ? i2c_master_transmit_receive(device, &reg, 1, value, 1, 20)
                  : ESP_ERR_INVALID_STATE;
}

static esp_err_t write_byte(uint8_t reg, uint8_t value)
{
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(device, bytes, sizeof(bytes), 20);
}

esp_err_t jelli_qmi_open(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x6b, .scl_speed_hz = 100000};
    return i2c_master_bus_add_device(bus, &config, &device);
}

static esp_err_t snapshot(uint8_t *values)
{
    for (unsigned i = 0; i < JELLI_QMI_REG_COUNT; ++i) {
        esp_err_t error = read_byte(registers[i], &values[i]);
        if (error != ESP_OK)
            return error;
    }
    return ESP_OK;
}

static esp_err_t wait_done(bool done, uint8_t *status)
{
    int64_t deadline = esp_timer_get_time() + 200000;
    for (unsigned i = 0; i < 40; ++i) {
        esp_err_t error = read_byte(0x2d, status);
        if (error != ESP_OK)
            return error;
        if (((*status & 0x80u) != 0) == done)
            return ESP_OK;
        if (esp_timer_get_time() >= deadline)
            break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t command(JelliQmiProbe *probe, uint8_t value)
{
    uint8_t status;
    esp_err_t error = read_byte(0x2d, &status);
    if (error != ESP_OK || (status & 0x80u))
        return error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
    error = write_byte(10, value);
    if (error == ESP_OK)
        error = wait_done(true, &status);
    /* ACK is cleanup, not a retry of the command, including ambiguous writes. */
    esp_err_t ack = write_byte(10, 0);
    if (ack == ESP_OK)
        ack = wait_done(false, &probe->ack_status);
    if (error == ESP_OK && ack == ESP_OK)
        ++probe->commands;
    return error == ESP_OK ? ack : error;
}

static esp_err_t identify(JelliQmiProbe *probe)
{
    esp_err_t error = command(probe, 0x10);
    for (unsigned i = 0; error == ESP_OK && i < 3; ++i)
        error = read_byte((uint8_t)(0x49u + i), &probe->firmware[i]);
    for (unsigned i = 0; error == ESP_OK && i < 6; ++i)
        error = read_byte((uint8_t)(0x51u + i), &probe->usid[i]);
    return error;
}

static esp_err_t arm(JelliQmiProbe *probe)
{
    /* 21 Hz, +/-2g, 128mg slope, INT2 initial low, 8 blanked samples.
     * Disable DRDY on INT2 so data-ready cannot masquerade as motion. */
    esp_err_t error = write_byte(3, 0x0d);
    if (error == ESP_OK)
        error = write_byte(11, 128);
    if (error == ESP_OK)
        error = write_byte(12, 0x48);
    if (error == ESP_OK) {
        probe->wom_attempted = true;
        error = command(probe, 8);
    }
    if (error == ESP_OK)
        error = write_byte(8, 0x21);
    return error;
}

esp_err_t jelli_qmi_begin(JelliQmiProbe *probe, bool motion)
{
    *probe = (JelliQmiProbe){0};
    esp_err_t error = read_byte(0, &probe->who);
    if (error == ESP_OK)
        error = read_byte(1, &probe->revision);
    if (error == ESP_OK && (probe->who != 5 || probe->revision != 0x7c))
        error = ESP_ERR_NOT_SUPPORTED;
    if (error == ESP_OK)
        error = snapshot(probe->before);
    if (error != ESP_OK)
        return error;
    /* Reject unknown owners/configurations; hidden prior WoM cannot be backed up. */
    if (memcmp(probe->before, baseline, sizeof(baseline)))
        return ESP_ERR_INVALID_STATE;
    probe->saved = true;
    probe->changed = true;
    error = write_byte(9, 0x80); /* Polling handshake, without INT1 assertion. */
    if (error == ESP_OK)
        error = identify(probe);
    if (error == ESP_OK && motion)
        error = arm(probe);
    return error;
}

esp_err_t jelli_qmi_ack_motion(JelliQmiProbe *probe)
{
    /* Caller records GPIO level before this clear-on-read operation. */
    esp_err_t error = read_byte(0x2f, &probe->status);
    probe->status_valid = error == ESP_OK;
    return error;
}

esp_err_t jelli_qmi_restore(JelliQmiProbe *probe)
{
    if (!probe->changed)
        return ESP_OK;
    esp_err_t result = write_byte(8, 0);
    if (probe->wom_attempted) {
        esp_err_t error = write_byte(11, 0);
        if (error == ESP_OK)
            error = command(probe, 8);
        if (error != ESP_OK)
            result = error;
    }
    /* Attempt each saved writable setting even if another cleanup failed.
     * CTRL9 is ACK-only, not an arbitrary replay of a saved command. */
    for (unsigned i = 0; i < JELLI_QMI_REG_COUNT; ++i) {
        esp_err_t error = write_byte(registers[i], probe->before[i]);
        if (error != ESP_OK)
            result = error;
    }
    esp_err_t readback = snapshot(probe->after);
    probe->restored = result == ESP_OK && readback == ESP_OK &&
                      !memcmp(probe->before, probe->after, sizeof(probe->before));
    return probe->restored ? ESP_OK : ESP_FAIL;
}
