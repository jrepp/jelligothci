#include "loki_models.h"
#include "loki_link.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(((JelliPrefsModels *)0)->names) == sizeof(((JelliLokiMessage *)0)->models),
               "a models message's names fit Settings' list as they are");

/* Written by the WebSocket task and by whichever task ends a session, read by the UI
 * and console tasks: copied whole under the spinlock. A "latest wins" slot cannot be
 * handed over without a lock, as a producer may not wait for a reader to finish. */
static JelliPrefsModels slot;
static portMUX_TYPE slot_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_uint generation;
static JelliPrefsModels console_copy; /* Console task only: what its commands read. */

void jelli_agent_models_offer(const JelliLokiMessage *message)
{
    if (!message || message->type != JELLI_LOKI_MODELS || !message->model_count ||
        message->model_count > JELLI_PREFS_MAX_MODELS || message->current >= message->model_count)
        return; /* The decoder already refused such lists; a second guard costs nothing. */
    taskENTER_CRITICAL(&slot_lock);
    memcpy(slot.names, message->models, sizeof(slot.names));
    slot.count = (uint8_t)message->model_count;
    slot.current = (uint8_t)message->current;
    atomic_fetch_add(&generation, 1u);
    taskEXIT_CRITICAL(&slot_lock);
}

void jelli_agent_models_forget(void)
{
    taskENTER_CRITICAL(&slot_lock);
    bool had = slot.count != 0u;
    memset(&slot, 0, sizeof(slot));
    if (had)
        atomic_fetch_add(&generation, 1u);
    taskEXIT_CRITICAL(&slot_lock);
}

uint32_t jelli_agent_models_generation(void) { return atomic_load(&generation); }

uint32_t jelli_agent_models_get(JelliPrefsModels *out)
{
    taskENTER_CRITICAL(&slot_lock);
    *out = slot;
    uint32_t now = atomic_load(&generation);
    taskEXIT_CRITICAL(&slot_lock);
    return now;
}

/* Appends text after the used bytes of out; false (out unchanged) when it does not fit. */
static bool append(char *out, size_t capacity, size_t *used, const char *text)
{
    size_t length = strlen(text);
    if (*used >= capacity || length >= capacity - *used)
        return false;
    memcpy(out + *used, text, length + 1u);
    *used += length;
    return true;
}

/* {"ok":true,"online":...,"current":N,"models":["a","b"]}: names need no escaping, as
 * their characters are A-Z a-z 0-9 . _ : / + -. */
void jelli_agent_models_show(char *out, size_t capacity)
{
    const JelliPrefsModels *shown = &console_copy;
    char head[80];
    size_t used = 0u;
    (void)jelli_agent_models_get(&console_copy);
    (void)snprintf(head, sizeof(head), "{\"ok\":true,\"online\":%s,\"current\":%u,\"models\":[",
                   jelli_agent_link_online() ? "true" : "false", (unsigned)shown->current);
    bool fits = capacity > 0u && append(out, capacity, &used, head);
    for (unsigned i = 0u; fits && i < shown->count; ++i)
        fits = append(out, capacity, &used, i ? ",\"" : "\"") &&
               append(out, capacity, &used, shown->names[i]) && append(out, capacity, &used, "\"");
    fits = fits && append(out, capacity, &used, "]}");
    if (!fits)
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"too_long\"}");
}

void jelli_agent_models_pick(const char *name, char *out, size_t capacity)
{
    (void)jelli_agent_models_get(&console_copy);
    bool listed = false;
    for (unsigned i = 0u; i < console_copy.count && !listed; ++i)
        listed = !strcmp(console_copy.names[i], name);
    if (!listed) {
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"not_offered\"}");
        return;
    }
    JelliLokiOut choice = {.type = JELLI_LOKI_SET_MODEL, .model = name};
    (void)snprintf(out, capacity,
                   jelli_agent_link_send(&choice) ? "{\"ok\":true}"
                                                  : "{\"ok\":false,\"error\":\"offline\"}");
}
