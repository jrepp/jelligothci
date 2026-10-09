#include "jelli/agent_console.h"
#include "jelli/agent_pmu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* Feeds text; returns how many requests completed, keeping the last one. */
static unsigned feed(JelliAgentConsole *console, const char *text, size_t size,
                     JelliAgentRequest *last)
{
    unsigned ready = 0u;
    for (size_t i = 0u; i < size; ++i)
        ready += jelli_agent_console_feed(console, text[i], last) ? 1u : 0u;
    return ready;
}

static unsigned feed_text(JelliAgentConsole *console, const char *text, JelliAgentRequest *last)
{
    return feed(console, text, strlen(text), last);
}

static void parses_requests(void)
{
    JelliAgentConsole console = {0};
    JelliAgentRequest request;
    CHECK(feed_text(&console, "@A1 7 probe te 1000\n", &request) == 1u);
    CHECK(request.id == 7u && request.count == 3u);
    CHECK(!strcmp(request.words[0], "probe") && !strcmp(request.words[1], "te") &&
          !strcmp(request.words[2], "1000"));
    /* CRLF, repeated spaces, and leading or trailing spaces are tolerated. */
    CHECK(feed_text(&console, "  @A1   4294967295  hello  \r\n", &request) == 1u);
    CHECK(request.id == UINT32_MAX && request.count == 1u && !strcmp(request.words[0], "hello"));
    /* Two requests in one chunk both complete. */
    CHECK(feed_text(&console, "@A1 1 a\n@A1 2 b\n", &request) == 2u);
    CHECK(request.id == 2u && !strcmp(request.words[0], "b"));
    /* Exactly the maximum number of words is accepted. */
    CHECK(feed_text(&console, "@A1 3 a b c d e f g h\n", &request) == 1u);
    CHECK(request.count == JELLI_AGENT_WORDS && !strcmp(request.words[7], "h"));
}

static void rejects_malformed_lines(void)
{
    static const char *const bad[] = {
        "@A1 1\n",                    /* no command */
        "@A1\n",                      /* no id */
        "\n",                         /* empty */
        "@J1 1 hello\n",              /* the pet's tag */
        "@a1 1 hello\n",              /* tag is case-sensitive */
        "@A1 x hello\n",              /* id is not decimal */
        "@A1 -1 hello\n",             /* no sign */
        "@A1 +1 hello\n",             /* no sign */
        "@A1 4294967296 hello\n",     /* id overflows */
        "@A1 99999999999999 hello\n", /* id overflows */
        "@A1 1 a b c d e f g h i\n",  /* one word too many */
        "@A1 1 he\tllo\n",            /* control byte */
        "@A1 1 he\x7fllo\n",          /* DEL */
        "@A1 1 h\xc3\xa9llo\n",       /* non-ASCII */
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        JelliAgentConsole console = {0};
        JelliAgentRequest request;
        CHECK(feed_text(&console, bad[i], &request) == 0u);
        /* The console recovers on the next line. */
        CHECK(feed_text(&console, "@A1 9 ok\n", &request) == 1u && request.id == 9u);
    }
    /* A NUL byte inside a line is a control byte too. */
    JelliAgentConsole console = {0};
    JelliAgentRequest request;
    CHECK(feed(&console, "@A1 1 a\0b\n", 10u, &request) == 0u);
}

static void bounds_line_length(void)
{
    JelliAgentConsole console = {0};
    JelliAgentRequest request;
    char line[JELLI_AGENT_LINE + 8u];
    /* The longest accepted line is LINE - 1 bytes before its LF. */
    memset(line, 'x', sizeof(line));
    memcpy(line, "@A1 5 ", 6u);
    line[JELLI_AGENT_LINE - 1u] = '\n';
    CHECK(feed(&console, line, JELLI_AGENT_LINE, &request) == 1u);
    CHECK(request.id == 5u && strlen(request.words[0]) == JELLI_AGENT_LINE - 7u);
    /* One byte longer is dropped through its LF, and the next line still parses. */
    line[JELLI_AGENT_LINE - 1u] = 'x';
    line[JELLI_AGENT_LINE] = '\n';
    CHECK(feed(&console, line, JELLI_AGENT_LINE + 1u, &request) == 0u);
    CHECK(feed_text(&console, "@A1 6 hello\n", &request) == 1u && request.id == 6u);
    /* Overflow mid-stream discards everything until LF, even a valid-looking tail. */
    for (unsigned i = 0u; i < 3u * JELLI_AGENT_LINE; ++i)
        CHECK(!jelli_agent_console_feed(&console, 'y', &request));
    CHECK(feed_text(&console, "@A1 7 hello\n", &request) == 0u);
    CHECK(feed_text(&console, "@A1 8 hello\n", &request) == 1u && request.id == 8u);
}

static void formats_replies(void)
{
    char out[32];
    size_t size = jelli_agent_console_reply(out, sizeof(out), 42u, "{\"ok\":true}");
    CHECK(size == strlen("\n@A1 42 {\"ok\":true}\n") && !strcmp(out, "\n@A1 42 {\"ok\":true}\n"));
    /* Exactly full (size + NUL == capacity) fits; one byte less does not. */
    char exact[21];
    CHECK(jelli_agent_console_reply(exact, sizeof(exact), 42u, "{\"ok\":true}") == 20u);
    char small[20];
    CHECK(jelli_agent_console_reply(small, sizeof(small), 42u, "{\"ok\":true}") == 0u);
    CHECK(small[0] == '\0');
    CHECK(jelli_agent_console_reply(NULL, 8u, 1u, "{}") == 0u);
    CHECK(jelli_agent_console_reply(out, 0u, 1u, "{}") == 0u);
}

static void numbers(void)
{
    uint32_t value = 99u;
    CHECK(jelli_agent_number("0", &value) && value == 0u);
    CHECK(jelli_agent_number("4294967295", &value) && value == UINT32_MAX);
    CHECK(!jelli_agent_number("4294967296", &value) && value == UINT32_MAX);
    CHECK(!jelli_agent_number("", &value) && !jelli_agent_number(NULL, &value));
    CHECK(!jelli_agent_number("12a", &value) && !jelli_agent_number(" 1", &value));
}

static void decodes_pmu(void)
{
    /* Every field at both ends of its range. */
    JelliAgentPmu low = jelli_agent_pmu_decode(0x00u, 0x00u, 0x00u, 0x00u);
    CHECK(!low.long_press_restarts && !low.long_press_off && !low.over_temperature_off);
    CHECK(low.on_ms == 128u && low.off_ms == 4000u && low.irq_ms == 1000u);
    CHECK(!low.aldo1_on && low.aldo1_mv == 500u);
    JelliAgentPmu high = jelli_agent_pmu_decode(0x07u, 0x3fu, 0x01u, 0x1eu);
    CHECK(high.long_press_restarts && high.long_press_off && high.over_temperature_off);
    CHECK(high.on_ms == 2000u && high.off_ms == 10000u && high.irq_ms == 2500u);
    CHECK(high.aldo1_on && high.aldo1_mv == 3500u);
    /* Fields are independent: unrelated bits never leak into them. */
    JelliAgentPmu mixed = jelli_agent_pmu_decode(0xf8u, 0xc6u, 0xfeu, 0xe1u);
    CHECK(!mixed.long_press_restarts && !mixed.long_press_off && !mixed.over_temperature_off);
    CHECK(mixed.on_ms == 1000u && mixed.off_ms == 6000u && mixed.irq_ms == 1000u);
    CHECK(!mixed.aldo1_on && mixed.aldo1_mv == 600u);
    /* 3.3 V, the usual codec supply, is code 28. */
    CHECK(jelli_agent_pmu_decode(0u, 0u, 1u, 28u).aldo1_mv == 3300u);
}

/* Independent reference: arithmetic progressions and division instead of masks and tables. */
static JelliAgentPmu reference_pmu(unsigned off, unsigned keys, unsigned ldo, unsigned volts)
{
    JelliAgentPmu pmu;
    pmu.long_press_restarts = off % 2u == 1u;
    pmu.long_press_off = off / 2u % 2u == 1u;
    pmu.over_temperature_off = off / 4u % 2u == 1u;
    switch (keys % 4u) {
    case 0u:
        pmu.on_ms = 128u;
        break;
    case 1u:
        pmu.on_ms = 512u;
        break;
    case 2u:
        pmu.on_ms = 1000u;
        break;
    default:
        pmu.on_ms = 2000u;
        break;
    }
    pmu.off_ms = 4000u + 2000u * (keys / 4u % 4u);
    pmu.irq_ms = 1000u + 500u * (keys / 16u % 4u);
    pmu.aldo1_on = ldo % 2u == 1u;
    pmu.aldo1_mv = 500u + 100u * (volts % 32u);
    return pmu;
}

static bool same_pmu(JelliAgentPmu a, JelliAgentPmu b)
{
    return a.aldo1_on == b.aldo1_on && a.long_press_off == b.long_press_off &&
           a.long_press_restarts == b.long_press_restarts &&
           a.over_temperature_off == b.over_temperature_off && a.aldo1_mv == b.aldo1_mv &&
           a.on_ms == b.on_ms && a.off_ms == b.off_ms && a.irq_ms == b.irq_ms;
}

/* All 65536 pairs of the two key registers, and every value of each LDO register
 * against fixed and varying neighbours, so no field reads another register's bits. */
static void decodes_pmu_exhaustively(void)
{
    for (unsigned off = 0u; off < 256u; ++off)
        for (unsigned keys = 0u; keys < 256u; ++keys) {
            unsigned ldo = (off * 7u + keys) & 0xffu, volts = (keys * 13u + off) & 0xffu;
            JelliAgentPmu got =
                jelli_agent_pmu_decode((uint8_t)off, (uint8_t)keys, (uint8_t)ldo, (uint8_t)volts);
            CHECK(same_pmu(got, reference_pmu(off, keys, ldo, volts)));
        }
    for (unsigned value = 0u; value < 256u; ++value)
        for (unsigned other = 0u; other < 256u; other += 51u) {
            CHECK(same_pmu(jelli_agent_pmu_decode((uint8_t)other, (uint8_t)other, (uint8_t)value,
                                                  (uint8_t)other),
                           reference_pmu(other, other, value, other)));
            CHECK(same_pmu(jelli_agent_pmu_decode((uint8_t)other, (uint8_t)other, (uint8_t)other,
                                                  (uint8_t)value),
                           reference_pmu(other, other, other, value)));
        }
}

int main(void)
{
    parses_requests();
    rejects_malformed_lines();
    bounds_line_length();
    formats_replies();
    numbers();
    decodes_pmu();
    decodes_pmu_exhaustively();
    puts("Agent console and PMU checks passed");
    return 0;
}
