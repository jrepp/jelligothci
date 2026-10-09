#include "jelli/agent_loki.h"
#include "jelli/agent_sha256.h"
#include "loki_schema.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The canonical form of docs/loki-protocol.md, for the golden vectors and for the
 * card digest an answer carries. It goes to a buffer, or straight into a hash. */
typedef struct {
    char *out;
    size_t capacity, used;
    bool overflow;
    JelliSha256 *sha;
} Writer;

static void emit(Writer *writer, const char *bytes, size_t count)
{
    if (writer->sha) {
        jelli_sha256_update(writer->sha, bytes, count);
    } else if (writer->overflow || count >= writer->capacity - writer->used) {
        writer->overflow = true; /* Leaves room for the NUL. */
    } else {
        memcpy(writer->out + writer->used, bytes, count);
        writer->used += count;
    }
}

static void emit_text(Writer *writer, const char *text) { emit(writer, text, strlen(text)); }

static void emit_quoted(Writer *writer, const char *text)
{
    char escaped[8];
    emit(writer, "\"", 1u);
    for (const unsigned char *at = (const unsigned char *)text; *at; ++at) {
        if (*at == '\\' || *at == '"')
            (void)snprintf(escaped, sizeof(escaped), "\\%c", *at);
        else if (*at == '\n')
            (void)snprintf(escaped, sizeof(escaped), "\\n");
        else if (*at < 0x20u || *at > 0x7Eu)
            (void)snprintf(escaped, sizeof(escaped), "\\x%02X", (unsigned)*at);
        else
            (void)snprintf(escaped, sizeof(escaped), "%c", *at);
        emit_text(writer, escaped);
    }
    emit(writer, "\"", 1u);
}

static void emit_number(Writer *writer, const JelliLokiField *field, const unsigned char *member)
{
    char number[16];
    if (field->kind == JELLI_LOKI_FIELD_U32) {
        uint32_t value;
        memcpy(&value, member, sizeof(value));
        (void)snprintf(number, sizeof(number), "%" PRIu32, value);
    } else {
        int32_t value;
        memcpy(&value, member, sizeof(value));
        (void)snprintf(number, sizeof(number), "%" PRId32, value);
    }
    emit_text(writer, number);
}

static void emit_value(Writer *writer, const JelliLokiField *field, const JelliLokiMessage *message)
{
    const unsigned char *member = (const unsigned char *)message + field->offset;
    bool flag;
    switch (field->kind) {
    case JELLI_LOKI_FIELD_U32:
    case JELLI_LOKI_FIELD_I32:
        emit_number(writer, field, member);
        break;
    case JELLI_LOKI_FIELD_BOOL:
        memcpy(&flag, member, sizeof(flag));
        emit_text(writer, flag ? "true" : "false");
        break;
    case JELLI_LOKI_FIELD_STATE: /* Bounded, since callers may build messages themselves. */
        emit_text(writer, jelli_loki_state_name((unsigned)message->state));
        break;
    case JELLI_LOKI_FIELD_KIND:
        emit_text(writer, jelli_loki_kind_name((unsigned)message->kind));
        break;
    case JELLI_LOKI_FIELD_OPTIONS:
        emit(writer, "[", 1u);
        for (unsigned i = 0u; i < message->option_count && i < JELLI_LOKI_OPTIONS; ++i) {
            if (i)
                emit(writer, ",", 1u);
            emit_quoted(writer, message->options[i]);
        }
        emit(writer, "]", 1u);
        break;
    case JELLI_LOKI_FIELD_TEXT:
    case JELLI_LOKI_FIELD_LINE:
    case JELLI_LOKI_FIELD_ID:
    case JELLI_LOKI_FIELD_HEX:
        emit_quoted(writer, (const char *)member);
        break;
    }
}

static void emit_field(Writer *writer, const JelliLokiField *field, const JelliLokiMessage *message,
                       bool first)
{
    if (!first)
        emit(writer, " ", 1u);
    emit_text(writer, field->name);
    emit(writer, "=", 1u);
    emit_value(writer, field, message);
}

size_t jelli_loki_canonical(const JelliLokiMessage *message, char *out, size_t capacity)
{
    Writer writer = {out, capacity, 0u, capacity == 0u, NULL};
    char seq[24];
    const JelliLokiTypeSpec *type = message && out ? jelli_loki_type_of(message->type) : NULL;
    if (!type)
        return 0u;
    emit_text(&writer, type->name);
    (void)snprintf(seq, sizeof(seq), " seq=%" PRIu32, message->seq);
    emit_text(&writer, seq);
    for (unsigned i = 0u; i < type->count; ++i)
        emit_field(&writer, &type->fields[i], message, false);
    if (writer.overflow)
        return 0u;
    out[writer.used] = '\0';
    return writer.used;
}

void jelli_loki_card_digest(const JelliLokiMessage *ask, char hex[JELLI_LOKI_DIGEST])
{
    JelliSha256 sha;
    uint8_t digest[JELLI_SHA256_SIZE];
    Writer writer = {NULL, 0u, 0u, false, &sha};
    jelli_sha256_init(&sha);
    for (unsigned i = 0u; i < JELLI_LOKI_CARD_FIELDS; ++i)
        emit_field(&writer, jelli_loki_card_field(i), ask, i == 0u);
    jelli_sha256_final(&sha, digest);
    for (size_t i = 0u; i < JELLI_SHA256_SIZE; ++i)
        (void)snprintf(hex + 2u * i, 3u, "%02x", (unsigned)digest[i]);
}
