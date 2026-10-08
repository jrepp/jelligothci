#include "jelli/debug.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static uint16_t pixels[(JELLI_WIDTH + 3u) * JELLI_HEIGHT];
static JelliPetEngine engine;
static JelliDebug debug;
static uint64_t clock_ms;

static uint64_t now(void *ctx)
{
    (void)ctx;
    return clock_ms;
}
static void present(void *ctx, const JelliSurface *surface)
{
    (void)ctx;
    (void)surface;
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

static int capture_tests(void)
{
    request("@J1 7 capture\n");
    CHECK(debug.captured && contains("\"capture\":1"));
    uint64_t ticks = engine.game.ticks;
    clock_ms += 2000u;
    CHECK(jelli_debug_frozen(&debug, &engine, clock_ms));
    request("@J1 8 press 0 1\n");
    CHECK(contains("busy_or_page_changed"));
    request("@J1 9 pixels 1 217155 2\n");
    CHECK(contains("range"));
    request("@J1 10 pixels 999 0 1\n");
    CHECK(contains("capture_expired"));
    /* Exact pixels across a padded row boundary; no padding may escape. */
    pixels[465] = 0x1234u;
    pixels[466] = 0xdeadu;
    pixels[469] = 0xabcdu;
    request("@J1 11 pixels 1 465 2\n");
    CHECK(contains("\"rgb565\":\"1234abcd\""));
    request("@J1 12 pixels 1 0 466\n");
    CHECK(debug.reply_size < JELLI_DEBUG_REPLY && debug.reply_size > 1864u);
    clock_ms += JELLI_DEBUG_IDLE_MS;
    CHECK(!jelli_debug_frozen(&debug, &engine, clock_ms));
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.game.ticks == ticks);
    request("@J1 13 release 1\n");
    CHECK(contains("capture_expired"));
    request("@J1 14 capture\n");
    CHECK(debug.captured);
    debug.capture_activity = clock_ms + JELLI_DEBUG_CAPTURE_MS;
    clock_ms += JELLI_DEBUG_CAPTURE_MS;
    CHECK(!jelli_debug_frozen(&debug, &engine, clock_ms));
    request("@J1 15 capture\n");
    request("@J1 16 release 3\n");
    CHECK(!debug.captured && contains("true"));
    return 0;
}

static int tunable_tests(void)
{
    request("@J1 80 tunables 1\n");
    CHECK(contains("idle_frame_ms") && contains("900"));
    request("@J1 81 tune 0 idle_frame_ms 1200\n");
    CHECK(contains("1200") && contains("global"));
    request("@J1 82 tune 1 idle_frame_ms 1800\n");
    CHECK(contains("1800") && contains("creature"));
    request("@J1 83 tune 1 idle_frame_ms 0\n");
    CHECK(contains("tunable_range"));
    request("@J1 84 tune 999 idle_frame_ms 900\n");
    CHECK(contains("invalid_scope"));
    request("@J1 85 tune 1 unknown 3\n");
    CHECK(contains("unknown_tunable"));
    request("@J1 86 capture\n");
    request("@J1 87 tune 1 idle_frame_ms 900\n");
    CHECK(contains("busy"));
    debug.captured = false;
    request("@J1 88 tune 1 idle_frame_ms reset\n");
    CHECK(contains("1200"));
    request("@J1 89 tune 0 idle_frame_ms reset\n");
    CHECK(contains("900"));
    return 0;
}

static int parser_tests(void)
{
    request("@J1 1 state\r\n");
    CHECK(contains("\"page\":\"home\""));
    CHECK(contains("\"label\":\"MENU\""));
    CHECK(contains("\"collection\":{\"owned_count\":0,\"owned_mask\":0,\"discovered_mask\":0"));
    request("@J1 1 press 0 0\n");
    request("@J1 2 press 0 1\n");
    CHECK(engine.ui.page == JELLI_UI_CARE);
    request("@J1 3 press 0 1\n");
    CHECK(contains("page_changed"));
    request("@J1 4 tap 4294967296 0\n");
    CHECK(contains("range"));
    request("@J1 5 tap -1 0\n");
    CHECK(contains("range"));
    request("@J1 6 tap 465 465\n");
    CHECK(engine.ui.page == JELLI_UI_CARE);
    request("@J1 7 press 1 0\n");
    CHECK(engine.ui.page == JELLI_UI_HOME);
    request("@J1 8 tap 0 0 extra\n");
    CHECK(contains("range"));
    request("log noise\n");
    CHECK(debug.reply_size == 0);
    for (unsigned i = 0; i < 200u; ++i)
        jelli_debug_feed(&debug, &engine, 'x', clock_ms);
    jelli_debug_feed(&debug, &engine, '\n', clock_ms);
    CHECK(debug.reply_size == 0);
    request("@J1 9 state\n");
    CHECK(contains("true"));
    size_t pending = debug.reply_size;
    jelli_debug_feed(&debug, &engine, 'x', clock_ms);
    CHECK(debug.reply_size == pending && debug.used == 0);
    return 0;
}

static int cheat_tests(void)
{
    request("@J1 40 cheat heal\n");
    CHECK(contains("applied"));
    const JelliPet *pet = &engine.game.pets[engine.game.active];
    request("@J1 41 cheat fullness 25\n");
    CHECK(contains("applied") && pet->needs[JELLI_SATIETY] == 250u);
    uint32_t revision = engine.game.revision;
    request("@J1 42 cheat fullness 101\n");
    CHECK(contains("range_or_name") && pet->needs[JELLI_SATIETY] == 250u);
    CHECK(engine.game.revision == revision);
    request("@J1 43 cheat gifts 21\n");
    CHECK(contains("range_or_name"));
    request("@J1 44 capture\n");
    CHECK(debug.captured);
    request("@J1 45 cheat heal\n");
    CHECK(contains("busy") && pet->needs[JELLI_SATIETY] == 250u);
    char release[48];
    (void)snprintf(release, sizeof(release), "@J1 46 release %" PRIu32 "\n", debug.capture_id);
    request(release);
    request("@J1 47 cheat heal\n");
    CHECK(contains("applied") && pet->needs[JELLI_SATIETY] == 1000u);
    CHECK(jelli_game_valid(&engine.game));
    return 0;
}

static int battery_tests(void)
{
    request("@J1 59 swipe 1\n"); /* Close the ring: the indicator shows on the resting scene. */
    CHECK(!engine.ui.menu_open);
    engine.ui.save_requested = false;
    engine.ui.power = (JelliPowerReading){JELLI_POWER_NO_BATTERY, JELLI_POWER_PERCENT_UNKNOWN};
    request("@J1 60 battery\n");
    CHECK(contains("\"state\":\"no-battery\",\"percent\":null,\"override\":false"));
    CHECK(contains("\"caption\":\"PLUGGED\""));
    request("@J1 61 battery charging 42\n");
    CHECK(contains("\"state\":\"charging\",\"percent\":42,\"override\":true"));
    CHECK(contains("\"icon\":12005,\"caption\":\"42%\""));
    CHECK(contains("\"host\":{\"state\":\"no-battery\",\"percent\":null}"));
    CHECK(jelli_pet_frame(&engine) && engine.ui.last_view.battery.state == JELLI_POWER_CHARGING);
    request("@J1 62 battery charging 101\n");
    CHECK(contains("battery_state_or_percent"));
    request("@J1 63 battery sideways 5\n");
    CHECK(contains("battery_state_or_percent"));
    request("@J1 64 battery charging\n");
    CHECK(contains("battery_state_or_percent"));
    request("@J1 65 battery clear extra\n");
    CHECK(contains("battery_state_or_percent"));
    CHECK(engine.ui.power_override.percent == 42u && engine.ui.power_override_active);
    request("@J1 66 battery discharging unknown\n");
    CHECK(contains("\"percent\":null") && contains("\"caption\":\"--%\""));
    request("@J1 67 battery discharging 7\n");
    CHECK(contains("\"caption\":\"LOW 7%\""));
    request("@J1 68 state\n");
    CHECK(contains("\"battery\":{\"display\":true,\"shown\":true,\"state\":\"discharging\""));
    CHECK(contains("\"buttons\":["));
    request("@J1 69 capture\n");
    request("@J1 70 battery clear\n");
    CHECK(contains("busy") && engine.ui.power_override_active);
    debug.captured = false;
    request("@J1 71 battery clear\n");
    CHECK(contains("\"state\":\"no-battery\"") && !engine.ui.power_override_active);
    CHECK(jelli_pet_frame(&engine) && engine.ui.last_view.battery.state == JELLI_POWER_NO_BATTERY);
    CHECK(engine.game.battery_display && !engine.ui.save_requested); /* Override is not saved. */
    return 0;
}

/* Malformed overrides are rejected without touching the override, game, or save. */
static int battery_parse_tests(void)
{
    static const char *const rejected[] = {"battery charging -1",
                                           "battery charging 999999999999",
                                           "battery charging 4294967296",
                                           "battery charging 4294967295",
                                           "battery charging abc",
                                           "battery charging 4a",
                                           "battery charging +5",
                                           "battery charging 0x10",
                                           "battery CHARGING 5",
                                           "battery 5 charging",
                                           "battery charging 42 extra",
                                           "battery clear now",
                                           "battery charging",
                                           "battery unknown",
                                           "battery charged -0",
                                           "battery discharging 101"};
    static uint8_t before[sizeof(JelliGame)], after[sizeof(JelliGame)];
    char line[96];
    request("@J1 80 battery discharging 33\n");
    CHECK(contains("\"caption\":\"33%\""));
    memcpy(before, &engine.game, sizeof(before)); /* Raw bytes: nothing may write the game. */
    engine.ui.save_requested = false;
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        CHECK(snprintf(line, sizeof(line), "@J1 %u %s\n", 81u + i, rejected[i]) > 0);
        request(line);
        CHECK(contains("battery_state_or_percent"));
        CHECK(engine.ui.power_override_active && engine.ui.power_override.percent == 33u);
    }
    memcpy(after, &engine.game, sizeof(after));
    CHECK(memcmp(before, after, sizeof(before)) == 0 && !engine.ui.save_requested);
    /* Over-long lines are discarded whole, never truncated into a valid command. */
    request("@J1 99 battery charging 5 "
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
    CHECK(debug.reply_size == 0u && engine.ui.power_override.percent == 33u);
    request("@J1 100 battery charging 0\n");
    CHECK(contains("\"percent\":0,") && contains("\"caption\":\"0%\""));
    request("@J1 101 battery charging 100\n");
    CHECK(contains("\"caption\":\"100%\"") && contains("\"icon\":12005"));
    request("@J1 102 battery unknown 50\n"); /* Unknown state ignores its percentage. */
    CHECK(contains("\"state\":\"unknown\",\"percent\":null,\"override\":true"));
    request("@J1 103 battery charged 7\n");
    CHECK(contains("\"icon\":12004,\"caption\":\"FULL\""));
    request("@J1 104 battery no-battery unknown\n");
    CHECK(contains("\"icon\":12006,\"caption\":\"PLUGGED\""));
    request("@J1 105 battery clear\n");
    request("@J1 106 battery clear\n"); /* Clearing twice is harmless. */
    CHECK(contains("\"override\":false") && !engine.ui.power_override_active);
    /* Hidden indicator: the reply still reports the reading, but shown is false. */
    engine.game.battery_display = false;
    CHECK(jelli_pet_frame(&engine));
    request("@J1 107 battery\n");
    CHECK(contains("{\"display\":false,\"shown\":false,"));
    engine.game.battery_display = true;
    /* Longest battery object still fits the full state reply on the busiest page. */
    request("@J1 108 battery no-battery 100\n");
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_COLLECTION;
    CHECK(jelli_pet_frame(&engine));
    request("@J1 109 state\n");
    CHECK(contains("\"override\":true") && contains("]}\n"));
    printf("state reply with battery: %zu of %u bytes\n", debug.reply_size, JELLI_DEBUG_REPLY);
    engine.ui.menu_open = false;
    engine.ui.page = JELLI_UI_HOME;
    request("@J1 110 battery clear\n");
    return 0;
}

int main(int argc, char **argv)
{
    JelliPlatform platform = {.now_ms = now, .present = present};
    JelliSurface surface = {
        .pixels = pixels, .width = JELLI_WIDTH, .height = JELLI_HEIGHT, .stride = JELLI_WIDTH + 3u};
    CHECK(jelli_pet_init(&engine, platform, surface));
    CHECK(jelli_pet_frame(&engine));
    /* Pipe fixture runs the identical protocol and renderer for host CLI tests. */
    if (argc == 2 && strcmp(argv[1], "--pipe") == 0) {
        int byte;
        while ((byte = getchar()) != EOF) {
            jelli_debug_feed(&debug, &engine, (char)byte, clock_ms);
            if (debug.reply_size) {
                CHECK(fwrite(debug.reply, 1u, debug.reply_size, stdout) == debug.reply_size);
                CHECK(fflush(stdout) == 0);
                debug.reply_size = 0;
                if (!debug.captured)
                    clock_ms += 100u; /* Deterministic host time for transition polling. */
                if (!jelli_debug_frozen(&debug, &engine, clock_ms))
                    CHECK(jelli_pet_frame(&engine));
            }
        }
        return 0;
    }
    CHECK(parser_tests() == 0);
    CHECK(capture_tests() == 0);
    CHECK(tunable_tests() == 0);
    CHECK(cheat_tests() == 0);
    CHECK(battery_tests() == 0);
    CHECK(battery_parse_tests() == 0);
    printf("debug state: %zu bytes\n", sizeof(JelliDebug));
    return 0;
}
