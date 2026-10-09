#include "jelli/agent_console.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TAG "@A1"

bool jelli_agent_number(const char *text, uint32_t *out)
{
    uint32_t value = 0u;
    if (!text || !*text)
        return false;
    for (size_t i = 0u; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return false;
        uint32_t digit = (uint32_t)(text[i] - '0');
        if (value > (UINT32_MAX - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

/* Splits the completed line in place at spaces. The tag and id take two words. */
static bool parse(JelliAgentConsole *console, JelliAgentRequest *request)
{
    const char *words[JELLI_AGENT_WORDS + 2u];
    unsigned count = 0u;
    bool start = true;
    console->line[console->used] = '\0';
    for (size_t i = 0u; i < console->used; ++i) {
        if (console->line[i] == ' ') {
            console->line[i] = '\0';
            start = true;
        } else if (start) {
            if (count == JELLI_AGENT_WORDS + 2u)
                return false;
            words[count++] = &console->line[i];
            start = false;
        }
    }
    uint32_t id = 0u;
    if (count < 3u || strcmp(words[0], TAG) != 0 || !jelli_agent_number(words[1], &id))
        return false;
    request->id = id;
    request->count = count - 2u;
    for (unsigned n = 0u; n < request->count; ++n)
        request->words[n] = words[n + 2u];
    return true;
}

bool jelli_agent_console_feed(JelliAgentConsole *console, char byte, JelliAgentRequest *request)
{
    if (byte == '\n') {
        bool ready = !console->discard && parse(console, request);
        console->used = 0u;
        console->discard = false;
        return ready;
    }
    if (byte == '\r' || console->discard)
        return false;
    /* Keep one byte for the terminator; printable ASCII only (bytes >= 0x80 included). */
    if (console->used + 1u >= sizeof(console->line) || byte < ' ' || byte > '~')
        console->discard = true;
    else
        console->line[console->used++] = byte;
    return false;
}

size_t jelli_agent_console_reply(char *out, size_t capacity, uint32_t id, const char *body)
{
    if (!out || !capacity)
        return 0u;
    int size = snprintf(out, capacity, "\n" TAG " %" PRIu32 " %s\n", id, body);
    if (size <= 0 || (size_t)size >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return (size_t)size;
}
