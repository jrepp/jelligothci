#ifndef JELLI_AGENT_LOKI_LINK_H
#define JELLI_AGENT_LOKI_LINK_H

#include "jelli/agent_loki.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* loki/1 to the paired Loki Bridge (docs/loki-protocol.md, RFC-003). A link task
 * keeps one mutually authenticated WebSocket up while the board is paired and on
 * Wi-Fi, reconnecting with backoff. Messages from the bridge are checked (hello and
 * welcome first, sequence numbers) and queued for the app; reply audio goes to the
 * player through the hooks below. */

/* Called on the WebSocket task, or session_ended on whichever task's send failed;
 * none may block or send. stop_audio is the bridge's request; session_ended drops the
 * reply silently, since its session is gone. */
typedef struct {
    void (*reply_begin)(uint32_t reply);
    void (*reply_audio)(uint32_t reply, const uint8_t *samples, size_t size);
    void (*reply_end)(uint32_t reply);
    void (*stop_audio)(void);
    void (*session_ended)(void);
} JelliAgentPlayerHooks;

/* Starts the link task; hooks may be NULL until a player exists. */
bool jelli_agent_link_start(const JelliAgentPlayerHooks *hooks);

/* True between the bridge's welcome and the end of that connection. */
bool jelli_agent_link_online(void);

/* Next message from the bridge for the app, waiting up to wait_ms. Welcome and
 * audio control are handled inside the link and not repeated here. */
bool jelli_agent_link_receive(JelliLokiMessage *message, uint32_t wait_ms);

/* Sends a control message (its seq is set here); false when offline or refused. */
bool jelli_agent_link_send(JelliLokiOut *message);

/* Sends one microphone frame: header fields plus mono samples. */
bool jelli_agent_link_send_audio(const JelliLokiAudio *audio, const int16_t *samples, size_t count);

/* JSON object for the console: online, sessions, failures, last error. */
void jelli_agent_link_status(char *out, size_t capacity);

#endif
