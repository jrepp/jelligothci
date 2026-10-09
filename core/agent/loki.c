#include "jelli/agent_loki.h"
#include "loki_json.h"
#include "loki_schema.h"
#include <string.h>

/* The decoder: strict JSON (loki_json.c) read against the field tables
 * (loki_schema.c), so the board and the bridge cannot read one message two ways. */

enum { NAME_BYTES = 32, VALUE_DEPTH = 4 };
#define SEQ_SEEN 0x80000000u /* Bit of seq among the seen fields. */

static bool is_id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

static bool valid_text(const JelliLokiField *field, const char *text, size_t length)
{
    bool id = field->kind == JELLI_LOKI_FIELD_ID, hex = field->kind == JELLI_LOKI_FIELD_HEX;
    if (field->kind == JELLI_LOKI_FIELD_LINE)
        return memchr(text, '\n', length) == NULL;
    if (id || hex) {
        if (!length || (hex && length != (size_t)field->min))
            return false;
        for (size_t i = 0u; i < length; ++i)
            if (id ? !is_id_char(text[i])
                   : !((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')))
                return false;
    }
    return true;
}

static bool read_word(JelliJson *json, int (*index_of)(const char *), int *index)
{
    char word[NAME_BYTES + 1];
    if (!jelli_json_string(json, word, NAME_BYTES, NULL))
        return false;
    *index = index_of(word);
    return *index >= 0;
}

static bool read_options(JelliJson *json, const JelliLokiField *field, JelliLokiMessage *message)
{
    unsigned count = 0u;
    size_t length;
    if (!jelli_json_take(json, '['))
        return false;
    if (!jelli_json_take(json, ']')) {
        do {
            if (count == JELLI_LOKI_OPTIONS ||
                !jelli_json_string(json, message->options[count], JELLI_LOKI_OPTION - 1u,
                                   &length) ||
                memchr(message->options[count], '\n', length))
                return false;
            ++count;
        } while (jelli_json_take(json, ','));
        if (!jelli_json_take(json, ']'))
            return false;
    }
    message->option_count = count;
    return count >= (unsigned)field->min && count <= (unsigned)field->max;
}

static bool read_number(JelliJson *json, const JelliLokiField *field, unsigned char *member)
{
    int64_t value;
    if (!jelli_json_integer(json, field->min, field->max, &value))
        return false;
    if (field->kind == JELLI_LOKI_FIELD_U32) {
        uint32_t narrow = (uint32_t)value;
        memcpy(member, &narrow, sizeof(narrow));
    } else {
        int32_t narrow = (int32_t)value;
        memcpy(member, &narrow, sizeof(narrow));
    }
    return true;
}

static bool read_field(JelliJson *json, const JelliLokiField *field, JelliLokiMessage *message)
{
    unsigned char *member = (unsigned char *)message + field->offset;
    size_t length;
    int index;
    bool flag;
    switch (field->kind) {
    case JELLI_LOKI_FIELD_U32:
    case JELLI_LOKI_FIELD_I32:
        return read_number(json, field, member);
    case JELLI_LOKI_FIELD_BOOL:
        if (!jelli_json_bool(json, &flag))
            return false;
        memcpy(member, &flag, sizeof(flag));
        return true;
    case JELLI_LOKI_FIELD_STATE:
        if (!read_word(json, jelli_loki_state_index, &index))
            return false;
        message->state = (JelliLokiState)index;
        return true;
    case JELLI_LOKI_FIELD_KIND:
        if (!read_word(json, jelli_loki_kind_index, &index))
            return false;
        message->kind = (JelliLokiKind)index;
        return true;
    case JELLI_LOKI_FIELD_OPTIONS:
        return read_options(json, field, message);
    case JELLI_LOKI_FIELD_TEXT:
    case JELLI_LOKI_FIELD_LINE:
    case JELLI_LOKI_FIELD_ID:
    case JELLI_LOKI_FIELD_HEX:
        return jelli_json_string(json, (char *)member, (size_t)field->max, &length) &&
               valid_text(field, (const char *)member, length);
    }
    return false;
}

/* One member after "t". The type's fields are read once each; unknown keys are
 * skipped, so newer bridges can add fields. */
static bool read_member(JelliJson *json, const JelliLokiTypeSpec *type, JelliLokiMessage *message,
                        uint32_t *seen)
{
    char key[NAME_BYTES + 1];
    int64_t seq;
    if (!jelli_json_string(json, key, NAME_BYTES, NULL) || !jelli_json_take(json, ':') ||
        !strcmp(key, "t"))
        return false;
    if (!strcmp(key, "seq")) {
        if (*seen & SEQ_SEEN || !jelli_json_integer(json, 1, UINT32_MAX, &seq))
            return false;
        *seen |= SEQ_SEEN;
        message->seq = (uint32_t)seq;
        return true;
    }
    for (unsigned i = 0u; type && i < type->count; ++i) {
        if (strcmp(key, type->fields[i].name) != 0)
            continue;
        if (*seen & (1u << i))
            return false;
        *seen |= 1u << i;
        return read_field(json, &type->fields[i], message);
    }
    return jelli_json_skip(json, VALUE_DEPTH);
}

static bool read_type(JelliJson *json, const JelliLokiTypeSpec **type)
{
    char key[NAME_BYTES + 1], name[NAME_BYTES + 1];
    if (!jelli_json_take(json, '{') || !jelli_json_string(json, key, NAME_BYTES, NULL) ||
        strcmp(key, "t") != 0 || !jelli_json_take(json, ':') ||
        !jelli_json_string(json, name, NAME_BYTES, NULL))
        return false;
    *type = jelli_loki_type_named(name);
    return true;
}

static bool complete(const JelliLokiTypeSpec *type, uint32_t seen, const JelliLokiMessage *message)
{
    for (unsigned i = 0u; i < type->count; ++i)
        if (type->fields[i].required && !(seen & (1u << i)))
            return false;
    return type->type != JELLI_LOKI_ASK || message->kind != JELLI_LOKI_APPROVE ||
           message->option_count == 2u;
}

JelliLokiResult jelli_loki_decode(const char *text, size_t size, JelliLokiMessage *message)
{
    JelliJson json;
    const JelliLokiTypeSpec *type;
    uint32_t seen = 0u;
    if (!message)
        return JELLI_LOKI_INVALID;
    /* Cleared first, so no refusal leaves an earlier message behind. */
    memset(message, 0, sizeof(*message));
    message->progress = -1;
    if (!text || size > JELLI_LOKI_MAX_IN)
        return JELLI_LOKI_INVALID;
    jelli_json_init(&json, text, size);
    if (!read_type(&json, &type))
        return JELLI_LOKI_INVALID;
    while (jelli_json_take(&json, ','))
        if (!read_member(&json, type, message, &seen))
            return JELLI_LOKI_INVALID;
    if (!jelli_json_take(&json, '}') || !jelli_json_end(&json) || !(seen & SEQ_SEEN))
        return JELLI_LOKI_INVALID;
    if (!type)
        return JELLI_LOKI_IGNORED; /* type stays JELLI_LOKI_UNKNOWN. */
    if (!complete(type, seen, message))
        return JELLI_LOKI_INVALID;
    message->type = type->type;
    return JELLI_LOKI_OK;
}
