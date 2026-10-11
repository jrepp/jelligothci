#include "inventory.h"
#include <string.h>

/* AXP2101 SWcharge v1.0 register map. No ADC enabling, W1C writes or gauge
 * programming. QMI8658C rev0.6: exclude clear-on-read status and FIFO.
 * PCF85063A: coherent calendar burst; controls/timer are observations only. */
static JelliInventory snapshots[JELLI_INVENTORY_COUNT] = {
    {.address = 0x34,
     .count = 25,
     .registers = {0x00, 0x01, 0x03, 0x18, 0x20, 0x21, 0x26, 0x30, 0x40, 0x41, 0x42, 0x48, 0x49,
                   0x4a, 0x61, 0x62, 0x63, 0x64, 0x80, 0x82, 0x90, 0x91, 0x92, 0x93, 0xa4}},
    {.address = 0x6b, .count = 9, .registers = {0, 1, 2, 3, 4, 5, 6, 7, 8}},
    {.address = 0x51, .count = 12, .registers = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 17}}};
static i2c_master_dev_handle_t devices[JELLI_INVENTORY_COUNT];
static esp_err_t errors[JELLI_INVENTORY_COUNT];

void jelli_inventory_init(i2c_master_bus_handle_t bus, esp_err_t bus_error)
{
    for (unsigned i = 0; i < JELLI_INVENTORY_COUNT; ++i) {
        errors[i] = bus_error;
        if (bus_error != ESP_OK)
            continue;
        i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                      .device_address = snapshots[i].address,
                                      .scl_speed_hz = 100000};
        errors[i] = i2c_master_bus_add_device(bus, &config, &devices[i]);
    }
}

void jelli_inventory_clear(void)
{
    for (unsigned i = 0; i < JELLI_INVENTORY_COUNT; ++i) {
        snapshots[i].valid = 0;
        memset(snapshots[i].values, 0, sizeof(snapshots[i].values));
    }
}

esp_err_t jelli_inventory_read(unsigned index)
{
    if (index >= JELLI_INVENTORY_COUNT)
        return ESP_ERR_INVALID_ARG;
    JelliInventory *s = &snapshots[index];
    s->valid = 0;
    if (errors[index] != ESP_OK)
        return errors[index];
    while (s->valid < s->count) {
        unsigned length = index == 2 && s->valid == 0 ? 11u : 1u;
        esp_err_t error = i2c_master_transmit_receive(devices[index], &s->registers[s->valid], 1,
                                                      &s->values[s->valid], length, 50);
        if (error != ESP_OK)
            return error;
        s->valid = (uint8_t)(s->valid + length);
    }
    if (index == 1 && s->values[0] != 0x05)
        return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

const JelliInventory *jelli_inventory_get(unsigned index)
{
    return index < JELLI_INVENTORY_COUNT ? &snapshots[index] : NULL;
}
