#ifndef JELLI_TESTS_LOKI_VECTORS_H
#define JELLI_TESTS_LOKI_VECTORS_H

#include <stddef.h>
#include <stdint.h>

/* tests/vectors/loki-1.tsv for the Loki protocol tests (docs/loki-protocol.md). */
typedef struct {
    const char *name, *direction, *verdict, *input, *canonical; /* canonical: ok only */
} LokiVector;

/* Reads the file named by LOKI_VECTORS once; exits on a malformed file. */
void loki_vectors_load(void);
unsigned loki_vector_count(void);
const LokiVector *loki_vector(unsigned index);
/* Exits when absent. */
const LokiVector *loki_vector_named(const char *name);

/* Decodes lowercase hex into out; exits when it is not hex or does not fit. */
size_t loki_unhex(const char *hex, uint8_t *out, size_t capacity);

/* A control-message input as bytes: the text itself, or a json-hex: input decoded
 * into scratch. Returns a pointer to the bytes and sets *size. */
const char *loki_control_bytes(const LokiVector *vector, uint8_t *scratch, size_t capacity,
                               size_t *size);

#endif
