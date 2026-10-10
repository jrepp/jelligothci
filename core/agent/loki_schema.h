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
    JELLI_LOKI_FIELD_MODEL, /* A model name (jelli_loki_model_name). */
    JELLI_LOKI_FIELD_OPTIONS,
    JELLI_LOKI_FIELD_MODELS
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

/* A field that is a list of strings (OPTIONS, MODELS): where its items and their
 * count (an unsigned) lie in JelliLokiMessage, and the kind each item is read as.
 * The field's min and max bound the count. */
typedef struct {
    size_t offset, count_offset;
    size_t item_size; /* Bytes per item, NUL included. */
    unsigned capacity;
    JelliLokiFieldKind item;
} JelliLokiList;

/* The layout of a list kind; NULL for any other kind. */
const JelliLokiList *jelli_loki_list(JelliLokiFieldKind kind);

/* A-Z a-z 0-9 _ -: the characters of an identifier. */
bool jelli_loki_id_char(char c);

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
