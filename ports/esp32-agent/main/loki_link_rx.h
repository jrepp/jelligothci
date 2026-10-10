#ifndef JELLI_AGENT_LOKI_LINK_RX_H
#define JELLI_AGENT_LOKI_LINK_RX_H

#include "loki_link.h"
#include "esp_event.h"
#include <stdbool.h>

/* Internal to the link (loki_link.c): the WebSocket task's side, which reassembles
 * messages, checks them, and routes them. */

/* FINISHED: the client's task has ended its session (its last event), so the client
 * and transports can be freed. */
enum {
    JELLI_LINK_CONNECTED = 1 << 0,
    JELLI_LINK_CLOSED = 1 << 1,
    JELLI_LINK_WELCOMED = 1 << 2,
    JELLI_LINK_FINISHED = 1 << 3
};

/* The WebSocket client's event handler for one session. */
void jelli_agent_link_on_event(void *ctx, esp_event_base_t base, int32_t id, void *data);
/* Forgets the last session's partial message and sequence numbers. */
void jelli_agent_link_rx_reset(void);
void jelli_agent_link_rx_hooks(const JelliAgentPlayerHooks *hooks);

/* Provided by loki_link.c. */
void jelli_agent_link_set(unsigned bits_to_set);
void jelli_agent_link_closed(const char *why);
void jelli_agent_link_abort(const char *why);
bool jelli_agent_link_deliver(const JelliLokiMessage *message);
void jelli_agent_link_send_hello(void);

#endif
