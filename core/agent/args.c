#include "jelli/agent_args.h"
#include <string.h>

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool jelli_agent_hex_decode(const char *text, uint8_t *out, size_t capacity, size_t *size)
{
    if (!text || !size)
        return false;
    size_t length = strlen(text);
    if (length % 2u || length / 2u > capacity || (length && !out))
        return false;
    for (size_t i = 0u; i < length; ++i)
        if (hex_digit(text[i]) < 0)
            return false;
    for (size_t i = 0u; i < length / 2u; ++i)
        out[i] = (uint8_t)(hex_digit(text[2u * i]) * 16 + hex_digit(text[2u * i + 1u]));
    *size = length / 2u;
    return true;
}

bool jelli_agent_ipv4_parse(const char *text, uint8_t address[4])
{
    uint8_t parts[4];
    if (!text || !address)
        return false;
    for (unsigned part = 0u; part < 4u; ++part) {
        unsigned value = 0u, digits = 0u;
        while (text[digits] >= '0' && text[digits] <= '9') {
            value = value * 10u + (unsigned)(text[digits] - '0');
            if (++digits > 3u || value > 255u)
                return false;
        }
        /* No empty parts and no leading zeros, which some parsers read as octal. */
        if (!digits || (digits > 1u && text[0] == '0'))
            return false;
        parts[part] = (uint8_t)value;
        text += digits;
        if (part < 3u && *text++ != '.')
            return false;
    }
    if (*text)
        return false;
    memcpy(address, parts, sizeof(parts));
    return true;
}

static bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* A name's last label starts with a letter (RFC 1123, 2.1), so no name reads as an
 * address: lwIP would take "010.0.0.1" as octal or "0x7f000001" as hex. */
bool jelli_agent_host_valid(const char *host)
{
    uint8_t address[4];
    size_t length = host ? strlen(host) : 0u, label = 0u, last = 0u;
    if (!length || length > 63u)
        return false;
    if (jelli_agent_ipv4_parse(host, address))
        return true;
    for (size_t i = 0u; i < length; ++i) {
        char c = host[i];
        if (c == '.') {
            if (!label || host[i - 1u] == '-')
                return false;
            label = 0u;
            last = i + 1u;
        } else if (is_letter(c) || (c >= '0' && c <= '9') || (c == '-' && label)) {
            ++label;
        } else {
            return false;
        }
    }
    return label && host[length - 1u] != '-' && is_letter(host[last]);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i)
        out[i] = (uint8_t)(value >> (8u * i));
}

void jelli_agent_frame_header(uint8_t out[JELLI_AGENT_FRAME_HEADER], uint8_t channels,
                              uint32_t sequence, uint32_t capture_ms)
{
    out[0] = 'J';
    out[1] = 'A';
    out[2] = 1u;
    out[3] = channels;
    put_u32(out + 4, sequence);
    put_u32(out + 8, capture_ms);
}
