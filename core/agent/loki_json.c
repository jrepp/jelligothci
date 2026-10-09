#include "loki_json.h"
#include <string.h>

void jelli_json_init(JelliJson *json, const char *text, size_t size)
{
    json->at = (const unsigned char *)text;
    json->end = json->at + size;
}

static void space(JelliJson *json)
{
    while (json->at < json->end &&
           (*json->at == ' ' || *json->at == '\t' || *json->at == '\n' || *json->at == '\r'))
        ++json->at;
}

bool jelli_json_take(JelliJson *json, char c)
{
    space(json);
    if (json->at < json->end && *json->at == (unsigned char)c) {
        ++json->at;
        return true;
    }
    return false;
}

bool jelli_json_end(JelliJson *json)
{
    space(json);
    return json->at == json->end;
}

static bool digit(const JelliJson *json, const unsigned char *at)
{
    return at < json->end && *at >= '0' && *at <= '9';
}

typedef struct {
    char *out;
    size_t max, size;
} Sink;

static bool put(Sink *sink, const unsigned char *bytes, size_t count)
{
    if (count > sink->max - sink->size)
        return false;
    if (sink->out)
        memcpy(sink->out + sink->size, bytes, count);
    sink->size += count;
    return true;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool hex4(JelliJson *json, uint32_t *value)
{
    uint32_t result = 0u;
    if (json->end - json->at < 4)
        return false;
    for (unsigned i = 0u; i < 4u; ++i) {
        int digit_value = hex_value(json->at[i]);
        if (digit_value < 0)
            return false;
        result = result * 16u + (uint32_t)digit_value;
    }
    json->at += 4;
    *value = result;
    return true;
}

/* After "\u": one code point, joining a high surrogate with the low one after it. */
static bool unicode_escape(JelliJson *json, uint32_t *code)
{
    uint32_t high, low;
    if (!hex4(json, &high) || (high >= 0xDC00u && high <= 0xDFFFu))
        return false;
    if (high < 0xD800u || high > 0xDBFFu) {
        *code = high;
        return true;
    }
    if (json->end - json->at < 2 || json->at[0] != '\\' || json->at[1] != 'u')
        return false;
    json->at += 2;
    if (!hex4(json, &low) || low < 0xDC00u || low > 0xDFFFu)
        return false;
    *code = 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
    return true;
}

static size_t utf8_encode(uint32_t code, unsigned char out[4])
{
    if (code < 0x80u) {
        out[0] = (unsigned char)code;
        return 1u;
    }
    if (code < 0x800u) {
        out[0] = (unsigned char)(0xC0u | (code >> 6));
        out[1] = (unsigned char)(0x80u | (code & 0x3Fu));
        return 2u;
    }
    if (code < 0x10000u) {
        out[0] = (unsigned char)(0xE0u | (code >> 12));
        out[1] = (unsigned char)(0x80u | ((code >> 6) & 0x3Fu));
        out[2] = (unsigned char)(0x80u | (code & 0x3Fu));
        return 3u;
    }
    out[0] = (unsigned char)(0xF0u | (code >> 18));
    out[1] = (unsigned char)(0x80u | ((code >> 12) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | ((code >> 6) & 0x3Fu));
    out[3] = (unsigned char)(0x80u | (code & 0x3Fu));
    return 4u;
}

/* After a backslash. \b, \f, \r, and \t would decode to control characters, which no
 * Loki string may hold, so they are rejected with the unknown escapes. */
static bool escape(JelliJson *json, Sink *sink)
{
    uint32_t code;
    unsigned char bytes[4];
    if (json->at >= json->end)
        return false;
    unsigned char c = *json->at++;
    if (c == '"' || c == '\\' || c == '/')
        code = c;
    else if (c == 'n')
        code = '\n';
    else if (c != 'u' || !unicode_escape(json, &code))
        return false;
    if (code < 0x20u && code != '\n')
        return false;
    size_t length = utf8_encode(code, bytes);
    return put(sink, bytes, length);
}

/* Length of the well-formed UTF-8 sequence at at (RFC 3629, no surrogates or
 * overlong forms), or 0. */
static size_t utf8_length(const unsigned char *at, const unsigned char *end)
{
    unsigned char c = at[0], low = 0x80u, high = 0xBFu;
    size_t length;
    if (c < 0x80u)
        return 1u;
    if (c >= 0xC2u && c <= 0xDFu)
        length = 2u;
    else if (c >= 0xE0u && c <= 0xEFu)
        length = 3u;
    else if (c >= 0xF0u && c <= 0xF4u)
        length = 4u;
    else
        return 0u;
    if (c == 0xE0u)
        low = 0xA0u;
    else if (c == 0xEDu)
        high = 0x9Fu;
    else if (c == 0xF0u)
        low = 0x90u;
    else if (c == 0xF4u)
        high = 0x8Fu;
    if ((size_t)(end - at) < length || at[1] < low || at[1] > high)
        return 0u;
    for (size_t i = 2u; i < length; ++i)
        if (at[i] < 0x80u || at[i] > 0xBFu)
            return 0u;
    return length;
}

bool jelli_json_string(JelliJson *json, char *out, size_t max_bytes, size_t *size)
{
    Sink sink = {out, max_bytes, 0u};
    if (!jelli_json_take(json, '"'))
        return false;
    for (;;) {
        if (json->at >= json->end)
            return false;
        unsigned char c = *json->at;
        if (c == '"') {
            ++json->at;
            break;
        }
        if (c == '\\') {
            ++json->at;
            if (!escape(json, &sink))
                return false;
            continue;
        }
        size_t length = c < 0x20u ? 0u : utf8_length(json->at, json->end);
        if (!length || !put(&sink, json->at, length))
            return false;
        json->at += length;
    }
    if (out)
        out[sink.size] = '\0';
    if (size)
        *size = sink.size;
    return true;
}

bool jelli_json_integer(JelliJson *json, int64_t min, int64_t max, int64_t *value)
{
    space(json);
    const unsigned char *at = json->at;
    bool negative = at < json->end && *at == '-';
    int64_t magnitude = 0;
    unsigned digits = 0u;
    if (negative)
        ++at;
    const unsigned char *first = at;
    for (; digit(json, at); ++at) {
        if (++digits > 18u) /* Far beyond every Loki range; keeps the value in int64_t. */
            return false;
        magnitude = magnitude * 10 + (*at - '0');
    }
    /* No digits, a leading zero, "-0", or a fraction or exponent. */
    if (!digits || (*first == '0' && (digits > 1u || negative)) ||
        (at < json->end && (*at == '.' || *at == 'e' || *at == 'E')))
        return false;
    int64_t result = negative ? -magnitude : magnitude;
    if (result < min || result > max)
        return false;
    json->at = at;
    *value = result;
    return true;
}

static bool literal(JelliJson *json, const char *word)
{
    size_t length = strlen(word);
    space(json);
    if ((size_t)(json->end - json->at) < length || memcmp(json->at, word, length) != 0)
        return false;
    json->at += length;
    return true;
}

bool jelli_json_bool(JelliJson *json, bool *value)
{
    if (literal(json, "true"))
        *value = true;
    else if (literal(json, "false"))
        *value = false;
    else
        return false;
    return true;
}

static const unsigned char *skip_digits(const JelliJson *json, const unsigned char *at)
{
    while (digit(json, at))
        ++at;
    return at;
}

/* Any JSON number: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)? */
static bool skip_number(JelliJson *json)
{
    const unsigned char *at = json->at;
    if (at < json->end && *at == '-')
        ++at;
    if (!digit(json, at))
        return false;
    at = *at == '0' ? at + 1 : skip_digits(json, at);
    if (at < json->end && *at == '.') {
        if (!digit(json, ++at))
            return false;
        at = skip_digits(json, at);
    }
    if (at < json->end && (*at == 'e' || *at == 'E')) {
        ++at;
        if (at < json->end && (*at == '+' || *at == '-'))
            ++at;
        if (!digit(json, at))
            return false;
        at = skip_digits(json, at);
    }
    json->at = at;
    return true;
}

/* At '[' or '{'; members may nest depth more levels. */
static bool skip_container(JelliJson *json, unsigned depth)
{
    bool object = *json->at == '{';
    char close = object ? '}' : ']';
    ++json->at;
    if (jelli_json_take(json, close))
        return true;
    do {
        if (object &&
            (!jelli_json_string(json, NULL, SIZE_MAX, NULL) || !jelli_json_take(json, ':')))
            return false;
        if (!jelli_json_skip(json, depth))
            return false;
    } while (jelli_json_take(json, ','));
    return jelli_json_take(json, close);
}

bool jelli_json_skip(JelliJson *json, unsigned depth)
{
    space(json);
    if (json->at >= json->end)
        return false;
    switch (*json->at) {
    case '"':
        return jelli_json_string(json, NULL, SIZE_MAX, NULL);
    case '{':
    case '[':
        return depth > 0u && skip_container(json, depth - 1u);
    case 't':
        return literal(json, "true");
    case 'f':
        return literal(json, "false");
    case 'n':
        return literal(json, "null");
    default:
        return skip_number(json);
    }
}
