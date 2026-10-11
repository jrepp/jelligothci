#ifndef JELLI_FACTORY_SUITE_H
#define JELLI_FACTORY_SUITE_H
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

enum { JELLI_FACTORY_TEST_COUNT = 10 };
typedef struct {
    const char *id, *status;
    bool supported;
    esp_err_t error;
    int64_t elapsed_us;
} JelliFactoryTest;
typedef struct {
    uint32_t boot, run;
    const char *phase, *scope;
    char device[13], elf_sha256[65];
    uint8_t pmic_values[4];
} JelliFactoryState;

/* Single main-task owner. Init allocates bounded scratch/I2C metadata once.
 * Begin only queues; poll executes at most one bounded probe between commands.
 * Results remain readable until the next explicit run or reboot. */
void jelli_factory_init(void);
const JelliFactoryState *jelli_factory_state(void);
const JelliFactoryTest *jelli_factory_test(unsigned index);
esp_err_t jelli_factory_begin(const char *scope);
void jelli_factory_poll(void);
#endif
