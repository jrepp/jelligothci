/* Debug protocol: the screen command, screen state, and debug input while dark. */
#include "jelli/debug.h"
#include <inttypes.h>
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

static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static JelliPetEngine engine;
static JelliDebug debug;
static uint64_t clock_ms;
static unsigned presents;

static uint64_t now(void *ctx)
{
    (void)ctx;
    return clock_ms;
}
static void present(void *ctx, const JelliSurface *surface)
{
    (void)ctx;
    (void)surface;
    ++presents;
}
static void request(const char *text)
{
    debug.reply_size = 0;
    for (size_t i = 0; text[i]; ++i)
        jelli_debug_feed(&debug, &engine, text[i], clock_ms);
}
static bool contains(const char *text)
{
    return debug.reply_size && strstr(debug.reply, text) != NULL;
}
static void frame_at(uint64_t time)
{
    clock_ms = time;
    CHECK(jelli_pet_frame(&engine));
}

static void status_and_transitions(void)
{
    request("@J1 1 screen\n");
    CHECK(contains("{\"ok\":true,\"screen\":{\"awake\":true,\"sleep_in_ms\":180000,"
                   "\"sleep_after_ms\":180000}}\n"));
    frame_at(61000u);
    request("@J1 2 screen\n");
    CHECK(contains("\"sleep_in_ms\":119000"));
    request("@J1 3 screen sleep\n");
    CHECK(contains("{\"awake\":false,\"sleep_in_ms\":0,"));
    bool awake = true;
    CHECK(jelli_pet_screen_changed(&engine, &awake) && !awake);
    unsigned before = presents;
    frame_at(62000u);
    CHECK(presents == before);
    request("@J1 4 screen sleep\n"); /* Idempotent. */
    CHECK(contains("\"awake\":false") && !jelli_pet_screen_changed(&engine, &awake));
    request("@J1 5 screen wake\n");
    CHECK(contains("{\"awake\":true,\"sleep_in_ms\":180000,") && !engine.ui.rendered);
    CHECK(jelli_pet_screen_changed(&engine, &awake) && awake);
    frame_at(62100u);
    CHECK(presents == before + 1u && engine.surface.damage.width == JELLI_WIDTH);
    request("@J1 6 screen wake\n"); /* Waking an awake screen only restarts the timer. */
    CHECK(contains("\"awake\":true") && !jelli_pet_screen_changed(&engine, &awake));
    /* Idle sleep shows up in status and state. */
    frame_at(62100u + JELLI_SCREEN_SLEEP_MS);
    request("@J1 7 screen\n");
    CHECK(contains("\"awake\":false"));
    request("@J1 8 state\n");
    CHECK(contains(",\"screen\":{\"awake\":false,\"sleep_in_ms\":0,\"sleep_after_ms\":180000},"));
    request("@J1 9 screen wake\n");
    request("@J1 10 state\n");
    CHECK(contains(",\"screen\":{\"awake\":true,\"sleep_in_ms\":180000,"));
}

static void malformed_and_busy(void)
{
    static const char *const rejected[] = {"screen nap",    "screen SLEEP", "screen sleep now",
                                           "screen wake 1", "screen 1",     "screen -"};
    char line[64];
    frame_at(clock_ms + 100u);
    for (unsigned i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        CHECK(snprintf(line, sizeof(line), "@J1 %u %s\n", 20u + i, rejected[i]) > 0);
        request(line);
        CHECK(contains("{\"ok\":false,\"error\":\"screen_action\"}"));
        CHECK(jelli_pet_screen_awake(&engine));
    }
    request("@J1 30 capture\n");
    CHECK(debug.captured);
    request("@J1 31 screen sleep\n");
    CHECK(contains("busy") && jelli_pet_screen_awake(&engine));
    request("@J1 32 screen\n"); /* Status stays readable during a capture. */
    CHECK(contains("\"awake\":true"));
    (void)snprintf(line, sizeof(line), "@J1 33 release %" PRIu32 "\n", debug.capture_id);
    request(line);
    CHECK(!debug.captured);
    request("@J1 34 screen sleep\n");
    request("@J1 35 capture\n"); /* The framebuffer is stale while dark. */
    CHECK(contains("screen_asleep") && !debug.captured);
    request("@J1 36 screen wake\n");
}

/* Debug input is trusted: it restarts the idle timer and wakes the screen at once.
 * Rejected input (range, busy) changes nothing. */
static void debug_input_is_activity(void)
{
    frame_at(clock_ms + 100u);
    uint64_t start = clock_ms;
    frame_at(start + 100000u);
    request("@J1 40 swipe 2\n");
    CHECK(contains("delivered"));
    request("@J1 41 screen\n");
    CHECK(contains("\"sleep_in_ms\":180000"));
    request("@J1 42 screen sleep\n");
    request("@J1 43 tap 466 0\n");
    CHECK(contains("range") && !jelli_pet_screen_awake(&engine));
    request("@J1 44 tap 0 0\n");
    CHECK(contains("delivered") && jelli_pet_screen_awake(&engine) && !engine.ui.rendered);
    frame_at(clock_ms + 100u);
    CHECK(engine.ui.page == JELLI_UI_HOME && !engine.ui.menu_open);
    request("@J1 45 screen sleep\n");
    request("@J1 46 press 0 0\n"); /* MENU on the home page opens the ring. */
    CHECK(contains("delivered") && jelli_pet_screen_awake(&engine) && engine.ui.menu_open);
    frame_at(clock_ms + 100u);
    request("@J1 47 screen sleep\n");
    request("@J1 48 press 4 1\n"); /* Stale page: rejected, and the screen stays dark. */
    CHECK(contains("busy_or_page_changed") && !jelli_pet_screen_awake(&engine));
    request("@J1 49 swipe 1\n"); /* Swipe down closes the ring and wakes the screen. */
    CHECK(contains("delivered") && jelli_pet_screen_awake(&engine) && !engine.ui.menu_open);
    frame_at(clock_ms + 100u);
}

/* The longest screen object still fits the full state reply on the busiest page. */
static void state_fits(void)
{
    request("@J1 60 battery no-battery 100\n");
    engine.game.sound_level = JELLI_SOUND_LEVEL_MAX;
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_COLLECTION;
    frame_at(clock_ms + 100u);
    request("@J1 61 screen sleep\n");
    request("@J1 62 state\n");
    CHECK(contains("\"screen\":{\"awake\":false") && contains("]}\n"));
    CHECK(debug.reply_size < JELLI_DEBUG_REPLY);
    request("@J1 63 screen wake\n");
    frame_at(clock_ms + 100u);
    request("@J1 64 state\n");
    CHECK(contains("\"sleep_in_ms\":179900,") && contains("]}\n"));
    printf("state reply with screen: %zu of %u bytes\n", debug.reply_size, JELLI_DEBUG_REPLY);
}

int main(void)
{
    JelliPlatform platform = {.now_ms = now, .present = present};
    JelliSurface surface = {
        .pixels = pixels, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = JELLI_WIDTH};
    CHECK(jelli_pet_init(&engine, platform, surface));
    CHECK(jelli_pet_frame(&engine));
    status_and_transitions();
    malformed_and_busy();
    debug_input_is_activity();
    state_fits();
    printf("Debug screen checks passed\n");
    return 0;
}
