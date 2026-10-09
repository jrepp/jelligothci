#ifndef JELLI_AGENT_CONSOLE_H
#define JELLI_AGENT_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USB serial console of the voice agent app (RFC-003). Framed like the pet's @J1
 * debug protocol (ADR-010) under its own tag, so neither client mistakes one app for
 * the other: requests are "@A1 <id> <word>...\n", replies "\n@A1 <id> <json>\n".
 * Portable and allocation-free; the host owns the transport and the commands. */
#define JELLI_AGENT_LINE 256u
#define JELLI_AGENT_WORDS 8u

typedef struct {
    char line[JELLI_AGENT_LINE];
    size_t used;
    bool discard;
} JelliAgentConsole;

typedef struct {
    uint32_t id;
    unsigned count; /* Words after the id, 1..JELLI_AGENT_WORDS. */
    const char *words[JELLI_AGENT_WORDS];
} JelliAgentRequest;

/* Zero-initialize the console. Feed one received byte; returns true when a complete,
 * well-formed request is in *request. Its words point into the console's line and
 * stay valid until the next feed. A line that overflows, contains a control or
 * non-ASCII byte, lacks the tag or a decimal id, or has too many words is dropped
 * through its LF. Carriage returns are ignored. */
bool jelli_agent_console_feed(JelliAgentConsole *console, char byte, JelliAgentRequest *request);

/* Formats "\n@A1 <id> <body>\n" into out. Returns its length, or 0 when it does not
 * fit (out then holds no partial reply that could be sent). */
size_t jelli_agent_console_reply(char *out, size_t capacity, uint32_t id, const char *body);

/* Strict decimal: digits only, no sign or spaces, at most UINT32_MAX. */
bool jelli_agent_number(const char *text, uint32_t *out);

#endif
