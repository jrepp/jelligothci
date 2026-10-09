#include "jelli/agent_args.h"
#include "jelli/agent_console.h"
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

static void decodes_hex(void)
{
    uint8_t out[4] = {0xeeu, 0xeeu, 0xeeu, 0xeeu};
    size_t size = 99u;
    CHECK(jelli_agent_hex_decode("00fFa7", out, sizeof(out), &size) && size == 3u);
    CHECK(out[0] == 0x00u && out[1] == 0xffu && out[2] == 0xa7u && out[3] == 0xeeu);
    /* Empty decodes to nothing, even without an output buffer. */
    CHECK(jelli_agent_hex_decode("", NULL, 0u, &size) && size == 0u);
    /* Exactly full fits; one byte more does not, and nothing is written on failure. */
    memset(out, 0xee, sizeof(out));
    CHECK(jelli_agent_hex_decode("01020304", out, 4u, &size) && size == 4u && out[3] == 4u);
    memset(out, 0xee, sizeof(out));
    size = 99u;
    CHECK(!jelli_agent_hex_decode("0102030405", out, 4u, &size) && size == 99u);
    static const char *const bad[] = {"0", "abc", "0g", "g0", " 00", "00 ", "0x00", "-1", "+1"};
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(!jelli_agent_hex_decode(bad[i], out, sizeof(out), &size));
        CHECK(size == 99u && out[0] == 0xeeu);
    }
    CHECK(!jelli_agent_hex_decode(NULL, out, sizeof(out), &size));
    CHECK(!jelli_agent_hex_decode("00", out, sizeof(out), NULL));
    CHECK(!jelli_agent_hex_decode("00", NULL, 1u, &size));
    /* A WPA passphrase of 63 printable bytes, spaces included, round-trips. */
    char hex[127];
    uint8_t secret[63], decoded[64];
    for (size_t i = 0u; i < sizeof(secret); ++i) {
        secret[i] = (uint8_t)(' ' + i % 95u);
        (void)snprintf(hex + 2u * i, 3u, "%02x", secret[i]);
    }
    CHECK(jelli_agent_hex_decode(hex, decoded, sizeof(decoded), &size) && size == 63u);
    CHECK(memcmp(decoded, secret, sizeof(secret)) == 0);
}

static void parses_ipv4(void)
{
    uint8_t address[4] = {9u, 9u, 9u, 9u};
    CHECK(jelli_agent_ipv4_parse("192.168.68.66", address));
    CHECK(address[0] == 192u && address[1] == 168u && address[2] == 68u && address[3] == 66u);
    CHECK(jelli_agent_ipv4_parse("0.0.0.0", address) && address[3] == 0u);
    CHECK(jelli_agent_ipv4_parse("255.255.255.255", address) && address[0] == 255u);
    static const char *const bad[] = {"",         "1.2.3",      "1.2.3.4.5", "1.2.3.4.",
                                      ".1.2.3.4", "1..2.3",     "256.1.1.1", "1.2.3.1000",
                                      "01.2.3.4", "1.2.3.04",   "1.2.3.-4",  "+1.2.3.4",
                                      " 1.2.3.4", "1.2.3.4 ",   "1.2.3.4a",  "a.b.c.d",
                                      "1,2,3,4",  "1.2.3.4/24", "localhost", "999.999.999.9"};
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        uint8_t untouched[4] = {7u, 7u, 7u, 7u};
        CHECK(!jelli_agent_ipv4_parse(bad[i], untouched));
        CHECK(untouched[0] == 7u && untouched[3] == 7u);
    }
    CHECK(!jelli_agent_ipv4_parse(NULL, address));
    CHECK(!jelli_agent_ipv4_parse("1.2.3.4", NULL));
}

static void writes_frame_headers(void)
{
    uint8_t out[JELLI_AGENT_FRAME_HEADER];
    jelli_agent_frame_header(out, 2u, 0x01020304u, 0xa0b0c0d0u);
    static const uint8_t expected[JELLI_AGENT_FRAME_HEADER] = {
        'J', 'A', 1u, 2u, 0x04u, 0x03u, 0x02u, 0x01u, 0xd0u, 0xc0u, 0xb0u, 0xa0u};
    CHECK(memcmp(out, expected, sizeof(out)) == 0);
}

/* The longest "wifi set" the client sends (32-byte SSID, 63-byte password, the largest
 * id) fits one console line and decodes into the firmware's 32- and 63-byte buffers. */
static void longest_wifi_request_fits_the_console(void)
{
    char line[JELLI_AGENT_LINE];
    int length = snprintf(line, sizeof(line), "@A1 4294967295 wifi set ");
    for (unsigned i = 0u; i < 32u + 1u + 63u && length > 0 && length < 250; ++i) {
        if (i == 32u)
            line[length++] = ' ';
        else
            length +=
                snprintf(line + length, sizeof(line) - (size_t)length, "%02x", 0xa0u + i % 64u);
    }
    CHECK(length == 215);
    line[length] = '\n';
    JelliAgentConsole console = {0};
    JelliAgentRequest request = {0};
    bool ready = false;
    for (int i = 0; i <= length; ++i)
        ready = jelli_agent_console_feed(&console, line[i], &request);
    CHECK(ready && request.id == 4294967295u && request.count == 4u);
    uint8_t ssid[32], password[63];
    size_t ssid_size = 0u, password_size = 0u;
    CHECK(jelli_agent_hex_decode(request.words[2], ssid, sizeof(ssid), &ssid_size));
    CHECK(jelli_agent_hex_decode(request.words[3], password, sizeof(password), &password_size));
    CHECK(ssid_size == 32u && password_size == 63u && ssid[0] == 0xa0u && password[62] == 0xbfu);
    /* One byte more no longer fits the SSID buffer. */
    CHECK(!jelli_agent_hex_decode(
              "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", ssid,
              sizeof(ssid), &ssid_size) &&
          ssid_size == 32u);
}

int main(void)
{
    decodes_hex();
    parses_ipv4();
    writes_frame_headers();
    longest_wifi_request_fits_the_console();
    puts("Agent argument and frame checks passed");
    return 0;
}
