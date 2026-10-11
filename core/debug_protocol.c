#include "jelli/debug_protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool jelli_debug_number(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    if (!text || !*text)
        return false;
    for (size_t i = 0; text[i]; ++i) {
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

void jelli_debug_protocol_response(JelliDebugProtocol *protocol, uint32_t id, const char *body)
{
    int size =
        snprintf(protocol->reply, sizeof(protocol->reply), "\n@J1 %" PRIu32 " %s\n", id, body);
    protocol->reply_size = size > 0 && (size_t)size < sizeof(protocol->reply) ? (size_t)size : 0u;
}

static void line(JelliDebugProtocol *protocol, JelliDebugDispatch dispatch, void *ctx)
{
    char *words[7];
    unsigned count = 0;
    bool start = true;
    for (size_t i = 0; i < protocol->used; ++i) {
        if (protocol->line[i] == ' ' || protocol->line[i] == '\r') {
            protocol->line[i] = '\0';
            start = true;
        } else if (start) {
            if (count == 7u)
                return;
            words[count++] = &protocol->line[i];
            start = false;
        }
    }
    uint32_t id;
    if (count >= 3u && !strcmp(words[0], "@J1") && jelli_debug_number(words[1], &id))
        dispatch(ctx, id, words, count);
}

void jelli_debug_protocol_feed(JelliDebugProtocol *protocol, char byte, JelliDebugDispatch dispatch,
                               void *ctx)
{
    if (protocol->reply_size)
        return;
    if (byte == '\n') {
        protocol->line[protocol->used] = '\0';
        if (!protocol->discard)
            line(protocol, dispatch, ctx);
        memset(protocol->line, 0, sizeof(protocol->line));
        protocol->used = 0;
        protocol->discard = false;
    } else if (protocol->used + 1u >= sizeof(protocol->line) || byte < ' ' || byte > '~') {
        if (byte != '\r')
            protocol->discard = true;
    } else if (!protocol->discard) {
        protocol->line[protocol->used++] = byte;
    }
}
