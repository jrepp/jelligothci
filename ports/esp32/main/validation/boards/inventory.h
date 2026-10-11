#ifndef JELLI_BOARD_INVENTORY_H
#define JELLI_BOARD_INVENTORY_H
#include "driver/i2c_master.h"
#include <stdint.h>

enum { JELLI_INVENTORY_COUNT = 3, JELLI_INVENTORY_REGS = 32 };
typedef struct {
    uint8_t address, count, valid;
    uint8_t registers[JELLI_INVENTORY_REGS], values[JELLI_INVENTORY_REGS];
} JelliInventory;

/* Startup-only handles; read-only allowlists, no sensor/status acknowledgements.
 * Single owner, fixed storage, no allocation during capture. */
void jelli_inventory_init(i2c_master_bus_handle_t bus, esp_err_t bus_error);
void jelli_inventory_clear(void);
esp_err_t jelli_inventory_read(unsigned index);
const JelliInventory *jelli_inventory_get(unsigned index);
#endif
