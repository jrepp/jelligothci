#include "qmi_probe.h"
#include "register_io.h"

static JelliRegisterIo io;

static esp_err_t native_result(JelliDeviceResult result)
{
    if (result >= 0)
        return (esp_err_t)result;
    switch (result) {
    case JELLI_DEVICE_IO:
        return ESP_FAIL;
    case JELLI_DEVICE_INVALID:
        return ESP_ERR_INVALID_ARG;
    case JELLI_DEVICE_UNAVAILABLE:
        return ESP_ERR_NOT_SUPPORTED;
    case JELLI_DEVICE_TIMEOUT:
        return ESP_ERR_TIMEOUT;
    case JELLI_DEVICE_STATE:
        return ESP_ERR_INVALID_STATE;
    default:
        return ESP_ERR_INVALID_RESPONSE;
    }
}

esp_err_t jelli_qmi_open(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x6b, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t device = NULL;
    esp_err_t error = i2c_master_bus_add_device(bus, &config, &device);
    io = jelli_esp_register_io(device);
    return error;
}

esp_err_t jelli_qmi_begin(JelliQmiProbe *probe, bool motion, bool enable_int2)
{
    return native_result(jelli_qmi8658_begin(io, probe, motion, enable_int2));
}

esp_err_t jelli_qmi_ack_motion(JelliQmiProbe *probe)
{
    return native_result(jelli_qmi8658_ack_motion(io, probe));
}

esp_err_t jelli_qmi_restore(JelliQmiProbe *probe)
{
    return native_result(jelli_qmi8658_restore(io, probe));
}
