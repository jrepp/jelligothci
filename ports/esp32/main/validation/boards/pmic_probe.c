#include "pmic_probe.h"

esp_err_t jelli_pmic_probe_open(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *device)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34, .scl_speed_hz = 100000};
    return i2c_master_bus_add_device(bus, &config, device);
}

esp_err_t jelli_pmic_probe_read(i2c_master_dev_handle_t device, uint8_t values[4])
{
    const uint8_t regs[] = {0x03, 0x26, 0x80, 0x90};
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        values[i] = 0;
        esp_err_t read = i2c_master_transmit_receive(device, &regs[i], 1, &values[i], 1, 50);
        if (read != ESP_OK)
            result = read;
    }
    return result;
}
