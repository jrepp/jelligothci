#include "../core/agent/prefs_internal.h"
#include "jelli/agent_draw.h"
#include "jelli/agent_prefs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings drawn with the agent's real font (ADR-016): every screen draws inside the
 * surface and its circle, the same way each time. Given a folder, it also writes each
 * screen there as a BMP for a person to look at. */

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum { SIDE = 466, STRIDE = 480, ROWS = 470, GUARD = 0x5a5a };
static uint16_t pixels[STRIDE * ROWS], again[STRIDE * ROWS];

static JelliSurface surface_of(uint16_t *memory)
{
    return (JelliSurface){memory, SIDE, SIDE, STRIDE, {0u, 0u, 0u, 0u}};
}

/* Draws the screen over guard values and checks nothing lands outside the surface or
 * outside the round panel, and that something was drawn. */
static void draw_checked(JelliPrefs *prefs, uint16_t *memory)
{
    for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
        memory[i] = GUARD;
    JelliSurface surface = surface_of(memory);
    prefs->dirty = true;
    jelli_prefs_draw(prefs, &surface);
    CHECK(!prefs->dirty && surface.damage.width == SIDE && surface.damage.height == SIDE);
    size_t lit = 0u;
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < STRIDE; ++x) {
            uint16_t pixel = memory[y * STRIDE + x];
            if (x >= SIDE || y >= SIDE) {
                CHECK(pixel == GUARD);
                continue;
            }
            int64_t dx = x - 233, dy = y - 233;
            if (pixel != 0u) {
                CHECK(dx * dx + dy * dy <= (int64_t)233 * 233); /* Inside the round panel. */
                ++lit;
            }
        }
    CHECK(lit > 500u);
}

static JelliPrefsNetwork network(const char *name, size_t size, int rssi,
                                 JelliPrefsSecurity security)
{
    JelliPrefsNetwork result = {{0}, (uint8_t)size, (int8_t)rssi, (uint8_t)security};
    memcpy(result.ssid, name, size);
    return result;
}

static void tap(JelliPrefs *prefs, PrefsBox box)
{
    JelliPrefsJoin join;
    (void)jelli_prefs_tap(prefs, box.x + box.width / 2, box.y + box.height / 2, 0u, &join);
}

/* BMP (bottom-up, 24-bit) with the corners outside the panel grey, as it looks. */
static void save(const char *folder, const char *name, const uint16_t *memory)
{
    if (!folder)
        return;
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/%s.bmp", folder, name);
    FILE *file = fopen(path, "wb");
    CHECK(file);
    uint8_t header[54] = {'B', 'M'};
    const uint32_t data = SIDE * (SIDE * 3u + 2u),
                   values[] = {54u + data, 0u, 54u, 40u, SIDE, SIDE};
    memcpy(header + 2, &values[0], 4u);
    memcpy(header + 10, &values[2], 16u); /* Offset, header size, width, height. */
    header[26] = 1u;
    header[28] = 24u;
    CHECK(fwrite(header, 1u, sizeof(header), file) == sizeof(header));
    for (int y = SIDE - 1; y >= 0; --y) {
        uint8_t row[SIDE * 3 + 2] = {0};
        for (int x = 0; x < SIDE; ++x) {
            uint16_t pixel = memory[y * STRIDE + x];
            int64_t dx = x - 233, dy = y - 233;
            bool outside = dx * dx + dy * dy > (int64_t)233 * 233;
            row[x * 3 + 0] = outside ? 90u : (uint8_t)((pixel & 31u) << 3);
            row[x * 3 + 1] = outside ? 90u : (uint8_t)(((pixel >> 5) & 63u) << 2);
            row[x * 3 + 2] = outside ? 90u : (uint8_t)((pixel >> 11) << 3);
        }
        CHECK(fwrite(row, 1u, sizeof(row), file) == sizeof(row));
    }
    CHECK(fclose(file) == 0);
}

static void shoot(JelliPrefs *prefs, const char *folder, const char *name)
{
    draw_checked(prefs, pixels);
    draw_checked(prefs, again);
    CHECK(!memcmp(pixels, again, sizeof(pixels))); /* The same state, the same pixels. */
    save(folder, name, pixels);
}

static void every_screen(const char *folder)
{
    JelliPrefs prefs;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, (const uint8_t *)"Home network 5G", 15u, true, 0u);
    shoot(&prefs, folder, "1-settings");
    tap(&prefs, prefs_box(PREFS_MENU_WIFI));
    prefs.frame = 2u;
    shoot(&prefs, folder, "2-searching");
    JelliPrefsNetwork found[] = {
        network("Home network 5G", 15u, -48, JELLI_PREFS_PASSWORD_NETWORK),
        network("A rather long network name!", 28u, -62, JELLI_PREFS_PASSWORD_NETWORK),
        network("Cafe", 4u, -71, JELLI_PREFS_OPEN_NETWORK),
        network("Office", 6u, -80, JELLI_PREFS_ENTERPRISE_NETWORK),
        network("\xe2\x98\x95 caf\xc3\xa9\x01\x7f", 11u, -85, JELLI_PREFS_PASSWORD_NETWORK),
        network("Neighbours", 10u, -90, JELLI_PREFS_PASSWORD_NETWORK),
    };
    jelli_prefs_scanned(&prefs, found, sizeof(found) / sizeof(found[0]), true, 0u);
    shoot(&prefs, folder, "3-networks");
    tap(&prefs, prefs_row_box(1u));
    shoot(&prefs, folder, "4-password-empty");
    memcpy(prefs.password, "hunter2", 8u);
    prefs.password_size = 7u;
    shoot(&prefs, folder, "5-password-short");
    memcpy(prefs.password, "correct horse battery", 22u);
    prefs.password_size = 21u;
    prefs.page = PREFS_UPPER;
    shoot(&prefs, folder, "6-password-capitals");
    prefs.reveal = true;
    prefs.page = PREFS_DIGITS;
    shoot(&prefs, folder, "7-password-digits-shown");
    prefs.page = PREFS_SYMBOLS;
    shoot(&prefs, folder, "8-password-symbols");
    JelliPrefsJoin join;
    PrefsKey keys[PREFS_MAX_KEYS];
    size_t count = prefs_keys(prefs.page, keys, PREFS_MAX_KEYS);
    (void)jelli_prefs_tap(&prefs, keys[count - 1u].box.x + 5, keys[count - 1u].box.y + 5, 0u,
                          &join);
    CHECK(prefs.screen == JELLI_PREFS_JOINING);
    prefs.frame = 3u;
    shoot(&prefs, folder, "9-joining");
    jelli_prefs_joined(&prefs, JELLI_PREFS_WRONG_PASSWORD, 0u);
    shoot(&prefs, folder, "10-wrong-password");
    prefs.outcome = JELLI_PREFS_NOT_FOUND;
    shoot(&prefs, folder, "11-not-found");
    prefs.outcome = JELLI_PREFS_FAILED;
    shoot(&prefs, folder, "12-failed");
    prefs.outcome = JELLI_PREFS_UNSUPPORTED;
    shoot(&prefs, folder, "13-unsupported");
    prefs.outcome = JELLI_PREFS_JOINED;
    shoot(&prefs, folder, "14-connected");
    prefs.outcome = JELLI_PREFS_NOT_LOCKED;
    shoot(&prefs, folder, "19-not-locked");
}

static void empty_states(const char *folder)
{
    JelliPrefs prefs;
    jelli_prefs_init(&prefs);
    jelli_prefs_open(&prefs, NULL, 0u, false, 0u);
    shoot(&prefs, folder, "15-settings-not-set-up");
    tap(&prefs, prefs_box(PREFS_MENU_WIFI));
    jelli_prefs_scanned(&prefs, NULL, 0u, true, 0u);
    shoot(&prefs, folder, "16-no-networks");
    tap(&prefs, prefs_box(PREFS_LIST_SCAN));
    jelli_prefs_scanned(&prefs, NULL, 0u, false, 0u);
    shoot(&prefs, folder, "17-search-failed");
    JelliPrefsNetwork many[12];
    for (unsigned i = 0u; i < 12u; ++i) {
        char name[] = "Net A";
        name[4] = (char)('A' + i);
        many[i] = network(name, 5u, -40 - (int)i * 4, JELLI_PREFS_PASSWORD_NETWORK);
    }
    tap(&prefs, prefs_box(PREFS_LIST_SCAN));
    jelli_prefs_scanned(&prefs, many, 12u, true, 0u);
    jelli_prefs_drag(&prefs, -PREFS_ROW_PITCH * 3);
    shoot(&prefs, folder, "18-scrolled");
}

/* The flat drawing primitives clip any coordinates and never write outside. */
static void primitives(void)
{
    for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
        pixels[i] = GUARD;
    JelliSurface surface = surface_of(pixels);
    jelli_agent_fill(&surface, -2147483647 - 1, -2147483647 - 1, 2147483647, 2147483647, 1u);
    jelli_agent_fill(&surface, 460, 460, 100, 100, 2u);
    jelli_agent_round_rect(&surface, -50, 400, 200, 200, 1000, 3u);
    jelli_agent_round_outline(&surface, 300, -20, 500, 90, 30, 50, 4u);
    jelli_agent_text(&surface, 400, 440, "overflowing \x01\xff text", 19u, 9u, 5u);
    jelli_agent_text(&surface, -30, -5, NULL, 3u, 2u, 6u);
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < STRIDE; ++x)
            if (x >= SIDE || y >= SIDE)
                CHECK(pixels[y * STRIDE + x] == GUARD);
    CHECK(pixels[465 * STRIDE + 465] == 2u);
    CHECK(jelli_agent_text_width(1000u, 0u) == JELLI_AGENT_TEXT_MAX_LENGTH * 8);
    CHECK(jelli_agent_text_width(3u, 100u) == 3 * 8 * JELLI_AGENT_TEXT_MAX_SCALE);
    CHECK(jelli_agent_rgb(200u, 162u, 255u) == 0xcd1fu);
    JelliSurface none = {NULL, SIDE, SIDE, STRIDE, {0u, 0u, 0u, 0u}};
    jelli_agent_fill(&none, 0, 0, 10, 10, 1u);
    jelli_agent_fill(NULL, 0, 0, 10, 10, 1u);
}

int main(int argc, char **argv)
{
    const char *folder = argc > 1 ? argv[1] : NULL;
    primitives();
    every_screen(folder);
    empty_states(folder);
    printf("Settings drawing checks passed%s%s\n", folder ? "; previews in " : "",
           folder ? folder : "");
    return 0;
}
