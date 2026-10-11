#ifndef JELLI_ESP_REGISTER_IO_H
#define JELLI_ESP_REGISTER_IO_H
#include "jelli/drivers/register_io.h"
#include "driver/i2c_master.h"

/* Caller owns one device handle from startup to shutdown. No global bus state. */
JelliRegisterIo jelli_esp_register_io(i2c_master_dev_handle_t device);
#endif
