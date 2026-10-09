/* A line-oriented driver for the board's loki/1 codec, so the bridge's tests can feed
 * the C and Swift implementations the same generated inputs and compare the results
 * (bridge/Tests/LokiProtocolTests/DifferentialTests.swift, tests/test_loki_diff.py).
 * One command per line on stdin, one result per line on stdout:
 *
 *   d xHEX                   decode a bridge-to-board control message
 *   a KIND CHANNELS xHEX     parse an audio frame of KIND (1 mic, 2 reply)
 *   h xHEX                   SHA-256 of the bytes
 *   e TYPE SEQ ARG...        encode a board-to-bridge message; strings are xHEX
 *
 * Bytes are written as "x" and lowercase hex, so an empty input is still a word.
 *
 * Results are "ok<TAB>..." (decode: the canonical form, and for ask the card digest;
 * encode: the message as hex), "ignore<TAB>SEQ", or "reject". Each input is copied
 * into a buffer of exactly its size, so a sanitizer build catches a read past it. */
#include "jelli/agent_loki.h"
#include "jelli/agent_sha256.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LINE = 16384, ARGS = 8, CANONICAL = 16384, STRING = 256 };

static void fail(const char *what)
{
    fprintf(stderr, "loki_diff: %s\n", what);
    exit(2);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* Decodes "x" and hex into a new buffer of exactly its length (at least one byte
 * allocated). */
static uint8_t *unhex(const char *arg, size_t *size)
{
    if (arg[0] != 'x')
        fail("bytes must start with x");
    const char *hex = arg + 1;
    size_t length = strlen(hex);
    if (length % 2u)
        fail("odd hex");
    uint8_t *bytes = malloc(length / 2u ? length / 2u : 1u);
    if (!bytes)
        fail("out of memory");
    for (size_t i = 0u; i < length / 2u; ++i) {
        int high = hex_digit(hex[2u * i]), low = hex_digit(hex[2u * i + 1u]);
        if (high < 0 || low < 0)
            fail("not hex");
        bytes[i] = (uint8_t)(high * 16 + low);
    }
    *size = length / 2u;
    return bytes;
}

static void print_hex(const uint8_t *bytes, size_t size)
{
    for (size_t i = 0u; i < size; ++i)
        printf("%02x", (unsigned)bytes[i]);
}

static void decode(const char *hex)
{
    static JelliLokiMessage message;
    static char canonical[CANONICAL];
    size_t size;
    uint8_t *bytes = unhex(hex, &size);
    JelliLokiResult result = jelli_loki_decode((const char *)bytes, size, &message);
    free(bytes);
    if (result == JELLI_LOKI_INVALID) {
        puts("reject");
        return;
    }
    if (result == JELLI_LOKI_IGNORED) {
        printf("ignore\t%" PRIu32 "\n", message.seq);
        return;
    }
    if (!jelli_loki_canonical(&message, canonical, sizeof(canonical)))
        fail("canonical form does not fit");
    printf("ok\t%s", canonical);
    if (message.type == JELLI_LOKI_ASK) {
        char digest[JELLI_LOKI_DIGEST];
        jelli_loki_card_digest(&message, digest);
        printf("\t%s", digest);
    }
    putchar('\n');
}

static long long number(const char *text)
{
    char *end = NULL;
    long long value = strtoll(text, &end, 10);
    if (!*text || *end)
        fail("not a number");
    return value;
}

static void audio(char **args)
{
    size_t size, payload = 0u;
    JelliLokiAudio parsed;
    uint8_t *frame = unhex(args[2], &size);
    bool ok = jelli_loki_audio_parse(frame, size, (JelliLokiAudioKind)number(args[0]),
                                     (unsigned)number(args[1]), &parsed, &payload);
    free(frame);
    if (!ok) {
        puts("reject");
        return;
    }
    printf("ok\taudio kind=%s final=%s stream=%" PRIu32 " seq=%" PRIu32 " bytes=%zu\n",
           parsed.kind == JELLI_LOKI_MIC ? "mic" : "reply", parsed.final ? "true" : "false",
           parsed.stream, parsed.seq, payload);
}

static void hash(const char *hex)
{
    JelliSha256 sha;
    uint8_t digest[JELLI_SHA256_SIZE];
    size_t size;
    uint8_t *bytes = unhex(hex, &size);
    jelli_sha256_init(&sha);
    jelli_sha256_update(&sha, bytes, size);
    jelli_sha256_final(&sha, digest);
    free(bytes);
    printf("ok\t");
    print_hex(digest, sizeof(digest));
    putchar('\n');
}

/* An encoder string argument: "x" and hex, into one of a few NUL-terminated slots. */
static const char *text(const char *arg, unsigned slot)
{
    static char strings[4][STRING];
    size_t size;
    if (slot >= 4u)
        fail("no string slot");
    uint8_t *bytes = unhex(arg, &size);
    if (size >= STRING || memchr(bytes, '\0', size))
        fail("string argument too long or holding NUL");
    memcpy(strings[slot], bytes, size);
    strings[slot][size] = '\0';
    free(bytes);
    return strings[slot];
}

/* The fields after TYPE and SEQ, in the protocol's order. */
static bool fill(JelliLokiOut *out, const char *type, unsigned count)
{
    static const struct {
        const char *name;
        JelliLokiOutType type;
        unsigned args;
    } TYPES[] = {{"hello", JELLI_LOKI_HELLO, 4u},     {"ptt_start", JELLI_LOKI_PTT_START, 1u},
                 {"ptt_end", JELLI_LOKI_PTT_END, 3u}, {"cancel", JELLI_LOKI_CANCEL, 0u},
                 {"answer", JELLI_LOKI_ANSWER, 4u},   {"played", JELLI_LOKI_PLAYED, 2u},
                 {"status", JELLI_LOKI_STATUS, 3u}};
    for (size_t i = 0u; i < sizeof(TYPES) / sizeof(TYPES[0]); ++i) {
        if (strcmp(type, TYPES[i].name) != 0)
            continue;
        if (count != TYPES[i].args)
            fail("wrong argument count");
        out->type = TYPES[i].type;
        return true;
    }
    return false;
}

static void fields(JelliLokiOut *out, char **args)
{
    switch (out->type) {
    case JELLI_LOKI_HELLO:
        out->fw = text(args[0], 0u);
        out->board = text(args[1], 1u);
        out->channels = (unsigned)number(args[2]);
        out->frame_ms = (unsigned)number(args[3]);
        break;
    case JELLI_LOKI_PTT_START:
        out->stream = (uint32_t)number(args[0]);
        break;
    case JELLI_LOKI_PTT_END:
        out->stream = (uint32_t)number(args[0]);
        out->frames = (uint32_t)number(args[1]);
        out->dropped = (uint32_t)number(args[2]);
        break;
    case JELLI_LOKI_ANSWER:
        out->id = text(args[0], 0u);
        out->nonce = text(args[1], 1u);
        out->digest = text(args[2], 2u);
        out->choice = (unsigned)number(args[3]);
        break;
    case JELLI_LOKI_PLAYED:
        out->reply = (uint32_t)number(args[0]);
        out->complete = number(args[1]) != 0;
        break;
    case JELLI_LOKI_STATUS:
        out->battery = (int)number(args[0]);
        out->charging = number(args[1]) != 0;
        out->rssi = (int)number(args[2]);
        break;
    case JELLI_LOKI_CANCEL:
    case JELLI_LOKI_NONE:
        break;
    }
}

static void encode(char **args, unsigned count)
{
    JelliLokiOut out = {0};
    char json[JELLI_LOKI_MAX_OUT + 1u];
    if (count < 2u || !fill(&out, args[0], count - 2u))
        fail("unknown message type");
    out.seq = (uint32_t)number(args[1]);
    fields(&out, args + 2);
    size_t size = jelli_loki_encode(&out, json, sizeof(json));
    if (!size) {
        puts("reject");
        return;
    }
    printf("ok\t");
    print_hex((const uint8_t *)json, size);
    putchar('\n');
}

/* Splits line at spaces into at most ARGS words; returns the count. */
static unsigned split(char *line, char **words)
{
    unsigned count = 0u;
    for (char *at = line; *at;) {
        while (*at == ' ')
            *at++ = '\0';
        if (!*at)
            break;
        if (count == ARGS)
            fail("too many arguments");
        words[count++] = at;
        while (*at && *at != ' ')
            ++at;
    }
    return count;
}

static void run(char *line)
{
    char *words[ARGS];
    unsigned count = split(line, words);
    if (!count)
        fail("empty command");
    if (!strcmp(words[0], "d") && count == 2u)
        decode(words[1]);
    else if (!strcmp(words[0], "a") && count == 4u)
        audio(words + 1);
    else if (!strcmp(words[0], "h") && count == 2u)
        hash(words[1]);
    else if (!strcmp(words[0], "e"))
        encode(words + 1, count - 1u);
    else
        fail("unknown command");
}

int main(void)
{
    static char line[LINE];
    while (fgets(line, sizeof(line), stdin)) {
        size_t length = strlen(line);
        if (length && line[length - 1u] != '\n' && !feof(stdin))
            fail("line too long");
        while (length && (line[length - 1u] == '\n' || line[length - 1u] == '\r'))
            line[--length] = '\0';
        run(line);
    }
    return fflush(stdout) == 0 && !ferror(stdin) ? 0 : 1;
}
