#ifndef JELLI_AGENT_LOKI_MODELS_H
#define JELLI_AGENT_LOKI_MODELS_H

#include "jelli/agent_console.h"
#include "jelli/agent_loki.h"
#include "jelli/agent_prefs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The models the bridge offers as the default (ADR-018), kept for Settings. The link
 * keeps only the latest models message here, never in the app inbox; the UI task
 * copies it whenever its generation changes. The slot is guarded by a spinlock held
 * only for one copy (about 660 bytes), so no task waits on another's work. */

/* WebSocket task: keeps a decoded models message as the latest list. */
void jelli_agent_models_offer(const JelliLokiMessage *message);

/* Any task: forgets the list, since the session that offered it ended. */
void jelli_agent_models_forget(void);

/* Any task: changes with every offer or forget. */
uint32_t jelli_agent_models_generation(void);

/* Any task: copies the latest list (count 0 when there is none) and returns the
 * generation it belongs to. */
uint32_t jelli_agent_models_get(JelliPrefsModels *out);

/* Console task: "loki models" shows the list; "loki model NAME" tells the bridge to
 * answer with that model, one the list offers, by default. */
void jelli_agent_models_show(char *out, size_t capacity);
void jelli_agent_models_pick(const char *name, char *out, size_t capacity);

#endif
