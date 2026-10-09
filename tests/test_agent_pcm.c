/* PCM reassembly for agent playback: examples, then seeded properties over random
 * messages split into random reads. A failure prints its seed so it can be pinned. */
#include "jelli/agent_pcm.h"
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

#define CHECK_SEED(expr, seed)                                                                     \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed %u)\n", __FILE__, __LINE__, #expr, (unsigned)(seed)); \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SEEDS = 5000u, MESSAGES = 6u, MESSAGE_BYTES = 700u, STREAM = MESSAGES * MESSAGE_BYTES };

/* Concatenates what reads yield, as link.c queues it for playback. */
typedef struct {
    uint8_t bytes[STREAM];
    size_t size;
} Output;

static void take(Output *out, const JelliAgentPcmParts *parts, const uint8_t *data, size_t size)
{
    CHECK(parts->joined_size == 0u || parts->joined_size == 2u);
    CHECK(parts->body_size % 2u == 0u);
    /* The body is the read's own bytes, never past its end. */
    CHECK(parts->body_size == 0u ||
          (parts->body >= data && parts->body + parts->body_size <= data + size));
    memcpy(out->bytes + out->size, parts->joined, parts->joined_size);
    out->size += parts->joined_size;
    if (parts->body_size)
        memcpy(out->bytes + out->size, parts->body, parts->body_size);
    out->size += parts->body_size;
}

static void feed(JelliAgentPcmJoin *join, Output *out, const uint8_t *data, size_t size, bool start)
{
    JelliAgentPcmParts parts;
    memset(&parts, 0xa5, sizeof(parts));
    jelli_agent_pcm_join(join, data, size, start, &parts);
    take(out, &parts, data, size);
}

static void even_reads_pass_through(void)
{
    static const uint8_t data[] = {1u, 2u, 3u, 4u};
    JelliAgentPcmJoin join = {0};
    JelliAgentPcmParts parts;
    jelli_agent_pcm_join(&join, data, sizeof(data), true, &parts);
    CHECK(parts.joined_size == 0u && parts.body == data && parts.body_size == 4u);
    CHECK(!join.carrying);
}

static void an_odd_tail_completes_the_next_read(void)
{
    static const uint8_t first[] = {1u, 2u, 3u}, second[] = {4u, 5u, 6u, 7u};
    JelliAgentPcmJoin join = {0};
    JelliAgentPcmParts parts;
    jelli_agent_pcm_join(&join, first, sizeof(first), true, &parts);
    CHECK(parts.joined_size == 0u && parts.body == first && parts.body_size == 2u);
    CHECK(join.carrying && join.carry == 3u);
    jelli_agent_pcm_join(&join, second, sizeof(second), false, &parts);
    CHECK(parts.joined_size == 2u && parts.joined[0] == 3u && parts.joined[1] == 4u);
    /* 5, 6 complete a sample; 7 waits. */
    CHECK(parts.body == second + 1 && parts.body_size == 2u);
    CHECK(join.carrying && join.carry == 7u);
}

static void single_byte_reads_alternate(void)
{
    static const uint8_t bytes[] = {10u, 11u, 12u, 13u, 14u};
    JelliAgentPcmJoin join = {0};
    Output out = {.size = 0u};
    for (size_t i = 0u; i < sizeof(bytes); ++i)
        feed(&join, &out, bytes + i, 1u, i == 0u);
    CHECK(out.size == 4u && !memcmp(out.bytes, bytes, 4u));
    CHECK(join.carrying && join.carry == 14u);
}

static void a_new_message_drops_a_stale_carry(void)
{
    static const uint8_t old[] = {0xeeu}, fresh[] = {1u, 2u, 3u, 4u};
    JelliAgentPcmJoin join = {0};
    JelliAgentPcmParts parts;
    jelli_agent_pcm_join(&join, old, sizeof(old), true, &parts);
    CHECK(join.carrying && parts.joined_size == 0u && parts.body_size == 0u);
    jelli_agent_pcm_join(&join, fresh, sizeof(fresh), true, &parts);
    CHECK(parts.joined_size == 0u && parts.body == fresh && parts.body_size == 4u);
    CHECK(!join.carrying);
}

/* A message fragmented into frames: a continuation frame's first read sits at payload
 * offset 0 but continues the message, so it keeps the carry; the next message drops it. */
static void a_continuation_frame_keeps_the_carry(void)
{
    static const uint8_t first[] = {1u, 2u, 3u}, continued[] = {4u, 5u}, next[] = {6u, 7u};
    JelliAgentPcmJoin join = {0};
    JelliAgentPcmParts parts;
    jelli_agent_pcm_join(&join, first, sizeof(first), true, &parts);
    CHECK(parts.body == first && parts.body_size == 2u && join.carrying && join.carry == 3u);
    jelli_agent_pcm_join(&join, continued, sizeof(continued), false, &parts);
    CHECK(parts.joined_size == 2u && parts.joined[0] == 3u && parts.joined[1] == 4u);
    CHECK(parts.body_size == 0u && join.carrying && join.carry == 5u);
    jelli_agent_pcm_join(&join, next, sizeof(next), true, &parts);
    CHECK(parts.joined_size == 0u && parts.body == next && parts.body_size == 2u);
    CHECK(!join.carrying);
}

static void empty_reads_change_nothing_but_a_start(void)
{
    JelliAgentPcmJoin join = {.carry = 9u, .carrying = true};
    JelliAgentPcmParts parts;
    memset(&parts, 0xa5, sizeof(parts));
    jelli_agent_pcm_join(&join, NULL, 0u, false, &parts);
    CHECK(parts.joined_size == 0u && parts.body_size == 0u && parts.body == NULL);
    CHECK(parts.joined[0] == 0u && parts.joined[1] == 0u);
    CHECK(join.carrying && join.carry == 9u);
    jelli_agent_pcm_join(&join, NULL, 0u, true, &parts);
    CHECK(!join.carrying && parts.joined_size == 0u && parts.body_size == 0u);
}

static void a_carried_byte_and_one_more_make_only_a_sample(void)
{
    static const uint8_t first[] = {1u}, second[] = {2u};
    JelliAgentPcmJoin join = {0};
    JelliAgentPcmParts parts;
    jelli_agent_pcm_join(&join, first, 1u, true, &parts);
    jelli_agent_pcm_join(&join, second, 1u, false, &parts);
    CHECK(parts.joined_size == 2u && parts.joined[0] == 1u && parts.joined[1] == 2u);
    CHECK(parts.body_size == 0u && !join.carrying);
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t below(uint32_t *state, uint32_t bound) { return next_random(state) % bound; }

typedef struct {
    uint8_t bytes[MESSAGES][MESSAGE_BYTES];
    size_t sizes[MESSAGES];
    size_t count;
} Messages;

/* Lengths favour the small and odd cases; a few messages are empty. */
static void make_messages(uint32_t *state, Messages *messages)
{
    messages->count = 1u + below(state, MESSAGES);
    for (size_t m = 0u; m < messages->count; ++m) {
        uint32_t shape = below(state, 4u);
        size_t size = shape == 0u ? below(state, 4u) : below(state, MESSAGE_BYTES + 1u);
        messages->sizes[m] = size;
        for (size_t i = 0u; i < size; ++i)
            messages->bytes[m][i] = (uint8_t)next_random(state);
    }
}

/* Feeds each message as reads of random size (0 allowed only when split is set). */
static void play(uint32_t *state, const Messages *messages, bool split, Output *out)
{
    JelliAgentPcmJoin join = {0};
    out->size = 0u;
    for (size_t m = 0u; m < messages->count; ++m) {
        size_t offset = 0u, size = messages->sizes[m];
        bool start = true;
        do {
            size_t left = size - offset;
            size_t read = split ? (left ? below(state, (uint32_t)left + 1u) : 0u) : left;
            if (split && below(state, 3u) == 0u && left)
                read = 1u + below(state, left < 3u ? (uint32_t)left : 3u);
            feed(&join, out, messages->bytes[m] + offset, read, start);
            CHECK(out->size % 2u == 0u); /* Never a half sample in the queue. */
            offset += read;
            start = false;
        } while (offset < size);
    }
}

/* link.c's rule: a read starts a message at payload offset 0 of a binary frame
 * (0x2), not of a continuation frame (0x0). */
static bool starts_message(size_t payload_offset, unsigned op_code)
{
    return payload_offset == 0u && op_code != 0x0u;
}

/* Feeds each message as a binary frame and continuation frames, each split into
 * reads, as esp_websocket_client delivers them; link.c skips empty reads, and so
 * frames here are never empty. */
static void play_frames(uint32_t *state, const Messages *messages, Output *out)
{
    JelliAgentPcmJoin join = {0};
    out->size = 0u;
    for (size_t m = 0u; m < messages->count; ++m) {
        size_t size = messages->sizes[m];
        unsigned op_code = 0x2u;
        for (size_t frame = 0u; frame < size;) {
            size_t frame_size = 1u + below(state, (uint32_t)(size - frame));
            for (size_t offset = 0u; offset < frame_size;) {
                size_t read = 1u + below(state, (uint32_t)(frame_size - offset));
                feed(&join, out, messages->bytes[m] + frame + offset, read,
                     starts_message(offset, op_code));
                CHECK(out->size % 2u == 0u);
                offset += read;
            }
            frame += frame_size;
            op_code = 0x0u;
        }
    }
}

/* Every message contributes its whole samples, byte-exact, whatever the split; a
 * message's odd last byte never reaches the next message. */
static void random_splits_reassemble_exactly(void)
{
    for (uint32_t seed = 1u; seed <= SEEDS; ++seed) {
        uint32_t state = seed * 2654435761u + 0x9e3779b9u;
        static Messages messages;
        static Output split, whole, framed, expected;
        make_messages(&state, &messages);
        expected.size = 0u;
        for (size_t m = 0u; m < messages.count; ++m) {
            size_t even = messages.sizes[m] & ~(size_t)1u;
            memcpy(expected.bytes + expected.size, messages.bytes[m], even);
            expected.size += even;
        }
        play(&state, &messages, true, &split);
        play(&state, &messages, false, &whole);
        play_frames(&state, &messages, &framed);
        /* Fragmenting into frames never matters either. */
        CHECK_SEED(framed.size == expected.size, seed);
        CHECK_SEED(!memcmp(framed.bytes, expected.bytes, expected.size), seed);
        CHECK_SEED(split.size == expected.size, seed);
        CHECK_SEED(!memcmp(split.bytes, expected.bytes, expected.size), seed);
        /* Metamorphic: the split never matters. */
        CHECK_SEED(whole.size == split.size && !memcmp(whole.bytes, split.bytes, split.size), seed);
    }
}

int main(void)
{
    even_reads_pass_through();
    an_odd_tail_completes_the_next_read();
    single_byte_reads_alternate();
    a_new_message_drops_a_stale_carry();
    a_continuation_frame_keeps_the_carry();
    empty_reads_change_nothing_but_a_start();
    a_carried_byte_and_one_more_make_only_a_sample();
    random_splits_reassemble_exactly();
    puts("agent pcm tests passed");
    return 0;
}
