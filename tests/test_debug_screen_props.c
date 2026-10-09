/* Seeded properties of the debug screen command. Over random clocks (including the top
 * of the 64-bit range) every `screen` reply has the exact documented shape, its numbers
 * stay in bounds and agree with the policy, and `state` carries the same object. Random
 * malformed arguments are always rejected with no effect, while tokenizer-equivalent
 * spellings are accepted. A capture blocks forced transitions and the idle rule alike,
 * and debug input refused while captured leaves a dark panel dark. */
#include "jelli/debug.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_CASE(expr)                                                                           \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s (seed 0x%08x case %u)\n", __FILE__, __LINE__, #expr,        \
                    (unsigned)seed, iteration);                                                    \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static JelliPetEngine engine;
static JelliDebug debug;
static uint64_t clock_ms;
static uint32_t seed, state;
static unsigned iteration;

static uint32_t next_random(void)
{
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

static uint64_t now(void *ctx)
{
    (void)ctx;
    return clock_ms;
}
static void present(void *ctx, const JelliSurface *surface)
{
    (void)ctx;
    (void)surface;
    CHECK_CASE(jelli_pet_screen_awake(&engine));
}
static const char *request(const char *text)
{
    debug.reply_size = 0u;
    for (size_t i = 0u; text[i]; ++i)
        jelli_debug_feed(&debug, &engine, text[i], clock_ms);
    CHECK_CASE(debug.reply_size > 0u && debug.reply[debug.reply_size - 1u] == '\n');
    return debug.reply;
}
static void reset(uint64_t at)
{
    clock_ms = at;
    JelliPlatform platform = {.now_ms = now, .present = present};
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    CHECK_CASE(jelli_pet_init(&engine, platform, surface));
    CHECK_CASE(jelli_pet_frame(&engine));
    memset(&debug, 0, sizeof(debug));
}

/* Parses exactly {"awake":B,"sleep_in_ms":N,"sleep_after_ms":180000} at `text`. */
static bool parse_screen(const char *text, bool *awake, uint64_t *remaining)
{
    static const char head[] = "{\"awake\":", middle[] = ",\"sleep_in_ms\":",
                      tail[] = ",\"sleep_after_ms\":180000}";
    if (strncmp(text, head, sizeof(head) - 1u) != 0)
        return false;
    text += sizeof(head) - 1u;
    *awake = strncmp(text, "true", 4u) == 0;
    if (!*awake && strncmp(text, "false", 5u) != 0)
        return false;
    text += *awake ? 4u : 5u;
    if (strncmp(text, middle, sizeof(middle) - 1u) != 0)
        return false;
    text += sizeof(middle) - 1u;
    if (*text < '0' || *text > '9' || (*text == '0' && text[1] >= '0' && text[1] <= '9'))
        return false;
    uint64_t value = 0u;
    for (unsigned digits = 0u; *text >= '0' && *text <= '9'; ++text) {
        value = value * 10u + (uint64_t)(*text - '0');
        if (++digits > 7u || value > 180000u)
            return false;
    }
    *remaining = value;
    return strncmp(text, tail, sizeof(tail) - 1u) == 0;
}

static void check_status(void)
{
    bool awake = false;
    uint64_t remaining = 0u;
    const char *reply = strchr(request("@J1 77 screen\n") + 1, '{');
    CHECK_CASE(reply && strncmp(reply, "{\"ok\":true,\"screen\":", 20u) == 0);
    CHECK_CASE(parse_screen(reply + 20, &awake, &remaining));
    CHECK_CASE(strcmp(reply + 20 + strcspn(reply + 20, "}") + 1, "}\n") == 0);
    CHECK_CASE(awake == jelli_pet_screen_awake(&engine) && (awake || remaining == 0u));
    CHECK_CASE(remaining == jelli_pet_screen_remaining(&engine.screen, clock_ms));
    char object[96];
    size_t length = strcspn(reply + 20, "}") + 1u;
    CHECK_CASE(length < sizeof(object));
    memcpy(object, reply + 20, length);
    object[length] = '\0';
    const char *full = request("@J1 78 state\n");
    const char *inside = strstr(full, ",\"screen\":");
    CHECK_CASE(inside && strncmp(inside + 10, object, length) == 0 && strstr(full, "]}\n"));
}

static void random_clocks(void)
{
    for (iteration = 0u; iteration < 150u; ++iteration) {
        seed = state = 0x1234567u + iteration * 0x9e3779b9u;
        uint64_t base = iteration % 4u == 0u ? UINT64_MAX - 400000u : (uint64_t)next_random() << 9;
        reset(base);
        for (unsigned step = 0u; step < 40u; ++step) {
            uint32_t pick = next_random() % 8u;
            uint64_t gap = pick < 4u ? next_random() % 1000u : next_random() % 200000u;
            if (pick == 7u) /* Backward. */
                clock_ms = clock_ms > gap ? clock_ms - gap : 0u;
            else
                clock_ms = UINT64_MAX - clock_ms < gap ? UINT64_MAX : clock_ms + gap;
            if (pick == 5u)
                (void)request(next_random() % 2u ? "@J1 3 screen sleep\n" : "@J1 3 screen wake\n");
            else
                CHECK_CASE(jelli_pet_frame(&engine));
            check_status();
        }
    }
}

/* A random token of printable ASCII, sometimes a near miss of a real action. */
static void random_word(char *out, size_t capacity)
{
    static const char *const near[] = {"sleep",  "wake",   "SLEEP", "Wake", "slee",
                                       "sleepy", "wakeup", "0",     "-1",   "sleep,",
                                       "awake",  "on",     "off"};
    uint32_t pick = next_random() % 4u;
    if (pick == 0u) {
        (void)snprintf(out, capacity, "%s", near[next_random() % 13u]);
        return;
    }
    size_t length = 1u + next_random() % 10u;
    for (size_t i = 0u; i < length && i + 1u < capacity; ++i)
        out[i] = (char)('!' + next_random() % 94u);
    out[length < capacity ? length : capacity - 1u] = '\0';
}

static void malformed_arguments(void)
{
    char line[96], words[3][16];
    reset(5000u);
    for (iteration = 0u; iteration < 3000u; ++iteration) {
        seed = state = 0xabcdef1u ^ (iteration * 0x85ebca6bu);
        if (!state)
            state = 1u;
        unsigned count = 1u + next_random() % 3u;
        for (unsigned i = 0u; i < count; ++i)
            random_word(words[i], sizeof(words[i]));
        bool valid = count == 1u && (!strcmp(words[0], "sleep") || !strcmp(words[0], "wake"));
        (void)snprintf(line, sizeof(line), "@J1 9 screen %s%s%s%s%s\n", words[0],
                       count > 1u ? " " : "", count > 1u ? words[1] : "", count > 2u ? " " : "",
                       count > 2u ? words[2] : "");
        bool was_awake = jelli_pet_screen_awake(&engine), rendered = engine.ui.rendered;
        uint64_t remaining = jelli_pet_screen_remaining(&engine.screen, clock_ms);
        const char *reply = request(line);
        if (valid) {
            CHECK_CASE(strstr(reply, "\"ok\":true"));
            CHECK_CASE(jelli_pet_screen_awake(&engine) == (words[0][0] == 'w'));
        } else {
            CHECK_CASE(strstr(reply, "{\"ok\":false,\"error\":\"screen_action\"}\n"));
            CHECK_CASE(jelli_pet_screen_awake(&engine) == was_awake);
            CHECK_CASE(engine.ui.rendered == rendered);
            CHECK_CASE(jelli_pet_screen_remaining(&engine.screen, clock_ms) == remaining);
        }
        clock_ms += 10u;
        CHECK_CASE(jelli_pet_frame(&engine));
    }
    /* A control byte discards the whole line through LF: no reply and no effect. */
    reset(9000u);
    debug.reply_size = 0u;
    static const char tabbed[] = "@J1 13 screen sleep\t\n";
    for (size_t i = 0u; tabbed[i]; ++i)
        jelli_debug_feed(&debug, &engine, tabbed[i], clock_ms);
    CHECK_CASE(debug.reply_size == 0u && jelli_pet_screen_awake(&engine));
    /* The tokenizer treats runs of spaces and CR as one separator. */
    CHECK_CASE(strstr(request("@J1 10 screen   sleep\r\n"), "\"awake\":false"));
    CHECK_CASE(strstr(request("@J1 11 screen wake \r\n"), "\"awake\":true"));
    CHECK_CASE(strstr(request("@J1 12 screen\r\n"), "\"ok\":true,\"screen\":"));
}

/* While captured no frame runs, so neither forced nor idle transitions can tear the
 * borrowed framebuffer; refused debug input leaves a dark panel dark. */
static void capture_interplay(void)
{
    iteration = 0u;
    reset(0u);
    clock_ms = 170000u;
    CHECK_CASE(jelli_pet_frame(&engine));
    CHECK_CASE(strstr(request("@J1 20 capture\n"), "\"ok\":true") && debug.captured);
    char line[64];
    for (uint64_t t = 170000u; t < 195000u; t += 1000u) { /* Host loop: frozen, no frames. */
        clock_ms = t;
        CHECK_CASE(jelli_debug_frozen(&debug, &engine, clock_ms));
        (void)snprintf(line, sizeof(line), "@J1 21 pixels %" PRIu32 " 0 8\n", debug.capture_id);
        CHECK_CASE(strstr(request(line), "\"rgb565\":\""));
        CHECK_CASE(strstr(request("@J1 22 screen sleep\n"), "\"error\":\"busy\""));
        CHECK_CASE(strstr(request("@J1 23 screen wake\n"), "\"error\":\"busy\""));
        CHECK_CASE(jelli_pet_screen_awake(&engine));
    }
    (void)snprintf(line, sizeof(line), "@J1 24 release %" PRIu32 "\n", debug.capture_id);
    CHECK_CASE(strstr(request(line), "\"ok\":true") && !debug.captured);
    CHECK_CASE(jelli_pet_frame(&engine));
    check_status(); /* Consistent either way; idle time is not forgiven by a capture. */
    /* Dark, then captured is impossible; dark and refused input stays dark. */
    (void)request("@J1 25 screen sleep\n");
    CHECK_CASE(strstr(request("@J1 26 capture\n"), "\"error\":\"screen_asleep\""));
    engine.game.resuming = true; /* Busy debug input: no wake, no delivery. */
    engine.game.resume_remaining_ms = 1000u;
    CHECK_CASE(strstr(request("@J1 27 tap 10 10\n"), "\"error\":\"busy\""));
    CHECK_CASE(strstr(request("@J1 28 swipe 1\n"), "\"error\":\"busy\""));
    CHECK_CASE(!jelli_pet_screen_awake(&engine));
    CHECK_CASE(strstr(request("@J1 29 tap 466 10\n"), "\"error\":\"range\""));
    CHECK_CASE(!jelli_pet_screen_awake(&engine));
}

int main(void)
{
    random_clocks();
    malformed_arguments();
    capture_interplay();
    printf("Debug screen properties passed: 150 clock walks, 3000 malformed lines\n");
    return 0;
}
