#include "loki_vectors.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LOKI_VECTORS /* CMake passes the absolute path; static analysis runs from the root. */
#define LOKI_VECTORS "tests/vectors/loki-1.tsv"
#endif

static char file[256 * 1024];
static LokiVector vectors[512];
static unsigned count;

static void fail(const char *what, const char *detail)
{
    fprintf(stderr, "%s: %s\n", what, detail);
    exit(1);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

size_t loki_unhex(const char *hex, uint8_t *out, size_t capacity)
{
    size_t length = strlen(hex);
    if (length % 2u || length / 2u > capacity)
        fail("hex does not fit", hex);
    for (size_t i = 0u; i < length / 2u; ++i) {
        int high = hex_digit(hex[2u * i]), low = hex_digit(hex[2u * i + 1u]);
        if (high < 0 || low < 0)
            fail("not hex", hex);
        out[i] = (uint8_t)(high * 16 + low);
    }
    return length / 2u;
}

/* One non-comment line: name, direction, verdict, input, and canonical for ok. */
static void add(char *line)
{
    char *fields[5] = {0};
    unsigned found = 0u;
    for (char *field = line; field && found < 5u; ++found) {
        fields[found] = field;
        field = strchr(field, '\t');
        if (field)
            *field++ = '\0';
    }
    if (found < 4u || count == sizeof(vectors) / sizeof(vectors[0]))
        fail("malformed vector", line);
    LokiVector *vector = &vectors[count++];
    *vector = (LokiVector){fields[0], fields[1], fields[2], fields[3], fields[4]};
    bool ok = !strcmp(vector->verdict, "ok");
    if (ok != (vector->canonical != NULL) ||
        (!ok && strcmp(vector->verdict, "ignore") != 0 && strcmp(vector->verdict, "reject") != 0) ||
        (strcmp(vector->direction, "b2d") != 0 && strcmp(vector->direction, "d2b") != 0))
        fail("malformed vector", vector->name);
}

static FILE *open_vectors(void)
{
#ifdef _MSC_VER
    FILE *stream = NULL;
    return fopen_s(&stream, LOKI_VECTORS, "rb") == 0 ? stream : NULL;
#else
    return fopen(LOKI_VECTORS, "rb");
#endif
}

void loki_vectors_load(void)
{
    if (count)
        return;
    FILE *stream = open_vectors();
    if (!stream)
        fail("cannot open", LOKI_VECTORS);
    size_t size = fread(file, 1u, sizeof(file) - 1u, stream);
    bool complete = feof(stream) && !ferror(stream);
    fclose(stream);
    if (!complete)
        fail("cannot read", LOKI_VECTORS);
    file[size] = '\0';
    for (char *line = file; line && *line;) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        if (*line && *line != '#')
            add(line);
        line = next;
    }
    if (count < 100u)
        fail("too few vectors in", LOKI_VECTORS);
}

unsigned loki_vector_count(void) { return count; }

const LokiVector *loki_vector(unsigned index) { return index < count ? &vectors[index] : NULL; }

const LokiVector *loki_vector_named(const char *name)
{
    for (unsigned i = 0u; i < count; ++i)
        if (!strcmp(vectors[i].name, name))
            return &vectors[i];
    fail("missing vector", name);
    return NULL;
}

const char *loki_control_bytes(const LokiVector *vector, uint8_t *scratch, size_t capacity,
                               size_t *size)
{
    if (!strncmp(vector->input, "json-hex:", 9u)) {
        *size = loki_unhex(vector->input + 9, scratch, capacity);
        return (const char *)scratch;
    }
    *size = strlen(vector->input);
    return vector->input;
}
