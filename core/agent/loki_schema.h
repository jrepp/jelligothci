#ifndef JELLI_LOKI_SCHEMA_H
#define JELLI_LOKI_SCHEMA_H

#include "jelli/agent_loki.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The loki/1 field tables behind the decoder (loki.c) and the canonical writer
 * (loki_canonical.c). Internal to core/agent. Each message type lists its fields in
 * the protocol's order (docs/loki-protocol.md), which is also the canonical order. */

typedef enum {
    JELLI_LOKI_FIELD_U32,
    JELLI_LOKI_FIELD_I32,
    JELLI_LOKI_FIELD_BOOL,
    JELLI_LOKI_FIELD_TEXT, /* Line feeds allowed. */
    JELLI_LOKI_FIELD_LINE, /* No line feeds. */
    JELLI_LOKI_FIELD_ID,
    JELLI_LOKI_FIELD_HEX,
    JELLI_LOKI_FIELD_STATE,
    JELLI_LOKI_FIELD_KIND,
    JELLI_LOKI_FIELD_OPTIONS
} JelliLokiFieldKind;

typedef struct {
    const char *name;
    JelliLokiFieldKind kind;
    bool required;
    size_t offset;    /* Of the member in JelliLokiMessage. */
    int64_t min, max; /* Numbers: the range. Strings: max bytes (HEX: exact length). */
} JelliLokiField;

typedef struct {
    const char *name;
    const JelliLokiField *fields;
    JelliLokiType type;
    unsigned count;
} JelliLokiTypeSpec;

/* NULL when the name or type is not one the board receives. */
const JelliLokiTypeSpec *jelli_loki_type_named(const char *name);
const JelliLokiTypeSpec *jelli_loki_type_of(JelliLokiType type);

/* The words of the state and kind enumerations; "?" when out of range. */
const char *jelli_loki_state_name(unsigned state);
const char *jelli_loki_kind_name(unsigned kind);
/* Index of word among the enumeration's words, or -1. */
int jelli_loki_state_index(const char *word);
int jelli_loki_kind_index(const char *word);

/* The ask fields that make up its card, in canonical order: kind, title, detail,
 * options. */
enum { JELLI_LOKI_CARD_FIELDS = 4 };
const JelliLokiField *jelli_loki_card_field(unsigned index);

#endif
