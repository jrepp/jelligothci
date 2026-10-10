#ifndef JELLI_AGENT_WIFI_INTERNAL_H
#define JELLI_AGENT_WIFI_INTERNAL_H

#include "esp_netif.h"
#include <stdbool.h>

/* wifi.c's lock, for the parts of Wi-Fi kept in other files (wifi_scan.c, wifi_status.c): take
 * returns false, without the lock, before Wi-Fi has started. */
bool jelli_agent_wifi_take(void);
void jelli_agent_wifi_give(void);

/* Whether Wi-Fi has started, and its station interface (NULL before). */
bool jelli_agent_wifi_started(void);
esp_netif_t *jelli_agent_wifi_station(void);

#endif
