#ifndef JELLI_AGENT_PREFS_WIFI_H
#define JELLI_AGENT_PREFS_WIFI_H

#include "jelli/agent_prefs.h"
#include <stdbool.h>
#include <stdint.h>

/* Settings' Wi-Fi errands (ADR-016): a scan or a join takes seconds, so a task of its
 * own runs them, one at a time, and the UI task collects each result on a later tick.
 * Start from the main task once Wi-Fi has started. */
bool jelli_agent_prefs_wifi_start(void);

/* Asks for a scan; false while another errand is under way (ask again later). UI task. */
bool jelli_agent_prefs_wifi_scan(void);

/* Asks to join; false while another errand is under way. The request is copied into
 * the errand's only slot, which is wiped once the join is done. UI task. */
bool jelli_agent_prefs_wifi_join(const JelliPrefsJoin *join);

/* Hands a finished errand's result to Settings, which ignores what it no longer
 * awaits. UI task. */
void jelli_agent_prefs_wifi_deliver(JelliPrefs *prefs, uint64_t now_ms);

#endif
