#include "prefs_draw.h"
#include <string.h>

void prefs_button(JelliSurface *surface, PrefsBox box, const char *label, PrefsButtonStyle style)
{
    int radius = box.height / 2;
    if (style == PREFS_PRIMARY)
        jelli_agent_round_rect(surface, box.x, box.y, box.width, box.height, radius, PREFS_LILAC);
    else if (style == PREFS_DISABLED)
        jelli_agent_round_rect(surface, box.x, box.y, box.width, box.height, radius, PREFS_KEY_DIM);
    else
        jelli_agent_round_outline(surface, box.x, box.y, box.width, box.height, radius, 2,
                                  PREFS_LILAC);
    uint16_t color = style == PREFS_PRIMARY    ? PREFS_BLACK
                     : style == PREFS_DISABLED ? PREFS_GREY
                                               : PREFS_WHITE;
    size_t length = strlen(label);
    int y = box.y + (box.height - JELLI_AGENT_GLYPH_HEIGHT * 2) / 2;
    jelli_agent_text_centered(surface, box.x + box.width / 2, y, label, length, 2u, color);
}

void prefs_card(JelliSurface *surface, PrefsBox box, const char *label, bool opens)
{
    jelli_agent_round_outline(surface, box.x, box.y, box.width, box.height, 22, 3,
                              opens ? PREFS_LILAC : PREFS_GREY);
    jelli_agent_text(surface, box.x + 24, box.y + 10, label, strlen(label), 3u, PREFS_WHITE);
}

/* The name as at most limit glyphs: whole, or its start and "..". */
static size_t shortened(const uint8_t *ssid, size_t size, size_t limit, char *out, size_t capacity)
{
    if (limit > capacity)
        limit = capacity;
    if (size > JELLI_PREFS_SSID_MAX)
        size = JELLI_PREFS_SSID_MAX;
    if (size <= limit) {
        memcpy(out, ssid, size);
        return size;
    }
    size_t kept = limit >= 2u ? limit - 2u : 0u;
    memcpy(out, ssid, kept);
    memcpy(out + kept, "..", limit - kept);
    return limit;
}

void prefs_name(JelliSurface *surface, int x, int y, const uint8_t *ssid, size_t size, size_t limit,
                uint16_t color)
{
    char name[JELLI_PREFS_SSID_MAX];
    size_t length = shortened(ssid, size, limit, name, sizeof(name));
    jelli_agent_text(surface, x, y, name, length, 2u, color);
}

void prefs_name_centered(JelliSurface *surface, int y, const uint8_t *ssid, size_t size,
                         size_t limit, uint16_t color)
{
    char name[JELLI_PREFS_SSID_MAX];
    size_t length = shortened(ssid, size, limit, name, sizeof(name));
    jelli_agent_text_centered(surface, PREFS_CENTER, y, name, length, 2u, color);
}

void prefs_model_name(JelliSurface *surface, int x, int y, const char *name, size_t limit,
                      uint16_t color)
{
    const char *end = memchr(name, '\0', JELLI_PREFS_MODEL_MAX + 1u);
    size_t size = end ? (size_t)(end - name) : JELLI_PREFS_MODEL_MAX + 1u;
    prefs_name(surface, x, y, (const uint8_t *)name, size, limit, color);
}

/* A padlock, 16 by 20: a shackle over a body. */
static void lock(JelliSurface *surface, int x, int y, uint16_t color)
{
    jelli_agent_round_outline(surface, x + 3, y, 10, 12, 5, 2, color);
    jelli_agent_fill(surface, x, y + 8, 16, 12, color);
}

/* One to four bars for the signal; the unlit ones stay visible, dimmer. */
static void bars(JelliSurface *surface, int x, int bottom, int rssi)
{
    int lit = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
    for (int bar = 0; bar < 4; ++bar) {
        int height = 6 * (bar + 1);
        jelli_agent_fill(surface, x + bar * 9, bottom - height, 6, height,
                         bar < lit ? PREFS_WHITE : PREFS_GREY);
    }
}

void prefs_network_row(JelliSurface *surface, PrefsBox box, const JelliPrefsNetwork *network,
                       bool current)
{
    jelli_agent_round_rect(surface, box.x, box.y, box.width, box.height, 10, PREFS_KEY);
    if (current)
        jelli_agent_round_outline(surface, box.x, box.y, box.width, box.height, 10, 3, PREFS_LILAC);
    prefs_name(surface, box.x + 14, box.y + (box.height - 24) / 2, network->ssid,
               network->ssid_size, 15u, current ? PREFS_LILAC : PREFS_WHITE);
    if (network->security != JELLI_PREFS_OPEN_NETWORK)
        lock(surface, box.x + box.width - 76, box.y + 12, PREFS_GREY);
    bars(surface, box.x + box.width - 48, box.y + box.height - 10, network->rssi);
}

void prefs_model_row(JelliSurface *surface, PrefsBox box, const char *name, bool current)
{
    jelli_agent_round_rect(surface, box.x, box.y, box.width, box.height, 10, PREFS_KEY);
    if (current)
        jelli_agent_round_outline(surface, box.x, box.y, box.width, box.height, 10, 3, PREFS_LILAC);
    prefs_model_name(surface, box.x + 14, box.y + (box.height - 24) / 2, name, 19u,
                     current ? PREFS_LILAC : PREFS_WHITE);
}

static const char *label(const PrefsKey *key, const JelliPrefs *prefs, char *one)
{
    switch (key->kind) {
    case PREFS_KEY_CHAR:
        one[0] = key->ch;
        one[1] = '\0';
        return one;
    case PREFS_KEY_SHIFT:
        return "^";
    case PREFS_KEY_MORE:
        return prefs->page == PREFS_DIGITS ? "#+=" : "123";
    case PREFS_KEY_DIGITS:
        return "123";
    case PREFS_KEY_LETTERS:
        return "abc";
    case PREFS_KEY_DELETE:
        return "<-";
    case PREFS_KEY_SPACE:
        return "space";
    case PREFS_KEY_JOIN:
        return "Join";
    }
    return "";
}

/* Join says how many more characters a password needs until it has enough. */
static void join_key(JelliSurface *surface, const PrefsKey *key, const JelliPrefs *prefs)
{
    if (prefs->password_size >= JELLI_PREFS_PASSWORD_MIN) {
        prefs_button(surface, key->box, "Join", PREFS_PRIMARY);
        return;
    }
    char more[] = "8 more";
    more[0] = (char)('0' + (JELLI_PREFS_PASSWORD_MIN - prefs->password_size));
    prefs_button(surface, key->box, more, PREFS_DISABLED);
}

void prefs_keyboard(JelliSurface *surface, const JelliPrefs *prefs)
{
    PrefsKey keys[PREFS_MAX_KEYS];
    size_t count = prefs_keys(prefs->page, keys, PREFS_MAX_KEYS);
    for (size_t i = 0u; i < count; ++i) {
        const PrefsKey *key = &keys[i];
        if (key->kind == PREFS_KEY_JOIN) {
            join_key(surface, key, prefs);
            continue;
        }
        /* Shift is lit while capitals are on: lilac, and its arrow turns black. */
        bool lit = key->kind == PREFS_KEY_SHIFT && prefs->page == PREFS_UPPER;
        PrefsBox box = key->box;
        jelli_agent_round_rect(surface, box.x, box.y, box.width, box.height, 8,
                               lit ? PREFS_LILAC : PREFS_KEY);
        char one[2];
        const char *text = label(key, prefs, one);
        int y = box.y + (box.height - JELLI_AGENT_GLYPH_HEIGHT * 2) / 2;
        jelli_agent_text_centered(surface, box.x + box.width / 2, y, text, strlen(text), 2u,
                                  lit ? PREFS_BLACK : PREFS_WHITE);
    }
}
