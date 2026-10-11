#include "experiments.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE wake_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t wake_count;

uint32_t jelli_experiments_network_wakes(void)
{
    portENTER_CRITICAL(&wake_lock);
    uint32_t count = wake_count;
    portEXIT_CRITICAL(&wake_lock);
    return count;
}

void jelli_experiments_network_wake(void)
{
    portENTER_CRITICAL(&wake_lock);
    ++wake_count;
    portEXIT_CRITICAL(&wake_lock);
}
