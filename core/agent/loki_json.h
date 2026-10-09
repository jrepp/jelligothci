#ifndef JELLI_LOKI_JSON_H
#define JELLI_LOKI_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The strict JSON reader behind jelli_loki_decode (docs/loki-protocol.md). Internal
 * to core/agent. Every function skips leading whitespace and, on failure, leaves the
 * reader at an unspecified position: the caller rejects the whole message. */

typedef struct {
    const unsigned char *at, *end;
} JelliJson;

void jelli_json_init(JelliJson *json, const char *text, size_t size);

/* Consumes c after optional whitespace; false (consuming nothing) when absent. */
bool jelli_json_take(JelliJson *json, char c);

/* True when only whitespace remains. */
bool jelli_json_end(JelliJson *json);

/* Reads a string of at most max_bytes UTF-8 bytes into out (max_bytes + 1 bytes,
 * NUL-terminated), or validates it when out is NULL. Rejects invalid UTF-8, unpaired
 * surrogates, and control characters other than line feed. *size gets the length. */
bool jelli_json_string(JelliJson *json, char *out, size_t max_bytes, size_t *size);

/* An integer without fraction, exponent, leading zero, or "-0", within [min, max]. */
bool jelli_json_integer(JelliJson *json, int64_t min, int64_t max, int64_t *value);

bool jelli_json_bool(JelliJson *json, bool *value);

/* Validates and skips any value, nesting arrays and objects at most depth deep. */
bool jelli_json_skip(JelliJson *json, unsigned depth);

#endif
