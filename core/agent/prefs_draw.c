#include "prefs_draw.h"
#include <string.h>

/* Settings on black, like the unicorn: white text in the pets' pixel font, lilac (the
 * agent's colour) for what is chosen or can be pressed, and grey for what is not.
 * State is never shown by colour alone: words, outlines, and shapes say it too. */

static void title(JelliSurface *surface, const char *text, int y)
{
    jelli_agent_text_centered(surface, PREFS_CENTER, y, text, strlen(text), 3u, PREFS_WHITE);
}

/* Searching or Joining, with one to three dots that never move the word. */
static void waiting(JelliSurface *surface, const char *word, int y, unsigned frame)
{
    size_t length = strlen(word);
    int x = PREFS_CENTER - jelli_agent_text_width(length + 3u, 2u) / 2;
    jelli_agent_text(surface, x, y, word, length, 2u, PREFS_WHITE);
    jelli_agent_text(surface, x + jelli_agent_text_width(length, 2u), y, "...", frame % 4u, 2u,
                     PREFS_WHITE);
}

static void draw_wifi_card(const JelliPrefs *prefs, JelliSurface *surface)
{
    PrefsBox card = prefs_box(PREFS_MENU_WIFI);
    prefs_card(surface, card, "Wi-Fi", true);
    const char *status = prefs->connected ? "online" : "offline";
    int status_width = jelli_agent_text_width(strlen(status), 2u);
    jelli_agent_text(surface, card.x + card.width - 24 - status_width, card.y + 16, status,
                     strlen(status), 2u, prefs->connected ? PREFS_LILAC : PREFS_GREY);
    if (prefs->current_size)
        prefs_name(surface, card.x + 24, card.y + 50, prefs->current, prefs->current_size, 17u,
                   prefs->connected ? PREFS_LILAC : PREFS_GREY);
    else
        jelli_agent_text(surface, card.x + 24, card.y + 50, "Not set up", 10u, 2u, PREFS_GREY);
}

/* The default model; without a list, why there is none, and no way in (grey). */
static void draw_model_card(const JelliPrefs *prefs, JelliSurface *surface)
{
    PrefsBox card = prefs_box(PREFS_MENU_MODEL);
    const char *name = jelli_prefs_model(prefs);
    prefs_card(surface, card, "Model", name != NULL);
    if (name) {
        prefs_model_name(surface, card.x + 24, card.y + 50, name, 17u, PREFS_LILAC);
        return;
    }
    const char *why = prefs->bridge_online ? "None offered" : "Bridge offline";
    jelli_agent_text(surface, card.x + 24, card.y + 50, why, strlen(why), 2u, PREFS_GREY);
}

static void draw_menu(const JelliPrefs *prefs, JelliSurface *surface)
{
    title(surface, "Settings", 58);
    draw_wifi_card(prefs, surface);
    draw_model_card(prefs, surface);
    prefs_button(surface, prefs_box(PREFS_MENU_DONE), "Done", PREFS_PRIMARY);
}

static void draw_scanning(const JelliPrefs *prefs, JelliSurface *surface)
{
    title(surface, "Wi-Fi", 70);
    waiting(surface, "Searching", 210, prefs->frame);
    prefs_button(surface, prefs_box(PREFS_SCAN_BACK), "Back", PREFS_SECONDARY);
}

/* Small triangles say more rows lie above or below. */
static void more(JelliSurface *surface, int y, bool up)
{
    for (int row = 0; row < 8; ++row) {
        int half = up ? row : 7 - row;
        jelli_agent_fill(surface, PREFS_CENTER - half, y + row, 2 * half + 1, 1, PREFS_GREY);
    }
}

static void draw_list(const JelliPrefs *prefs, JelliSurface *surface)
{
    title(surface, "Wi-Fi", 46);
    if (!prefs->count) {
        const char *why = prefs->scan_failed ? "Couldn't search" : "No networks found";
        jelli_agent_text_centered(surface, PREFS_CENTER, 200, why, strlen(why), 2u, PREFS_WHITE);
    }
    for (unsigned row = 0u; row < PREFS_LIST_ROWS && prefs->top + row < prefs->count; ++row) {
        const JelliPrefsNetwork *network = &prefs->networks[prefs->top + row];
        /* The board's own: its name, and locked as it is. */
        bool current = prefs->current_size && network->ssid_size == prefs->current_size &&
                       !memcmp(network->ssid, prefs->current, prefs->current_size) &&
                       (network->security != JELLI_PREFS_OPEN_NETWORK) == prefs->current_secured;
        prefs_network_row(surface, prefs_row_box(row), network, current);
    }
    if (prefs->top)
        more(surface, 88, true);
    if (prefs->top + PREFS_LIST_ROWS < prefs->count)
        more(surface, 344, false);
    prefs_button(surface, prefs_box(PREFS_LIST_BACK), "Back", PREFS_SECONDARY);
    prefs_button(surface, prefs_box(PREFS_LIST_SCAN), "Scan", PREFS_SECONDARY);
}

static void draw_models(const JelliPrefs *prefs, JelliSurface *surface)
{
    const JelliPrefsModels *models = &prefs->models;
    title(surface, "Model", 46);
    for (unsigned row = 0u; row < PREFS_LIST_ROWS && prefs->model_top + row < models->count;
         ++row) {
        unsigned index = prefs->model_top + row;
        prefs_model_row(surface, prefs_row_box(row), models->names[index],
                        index == models->current);
    }
    if (prefs->model_top)
        more(surface, 88, true);
    if (prefs->model_top + PREFS_LIST_ROWS < models->count)
        more(surface, 344, false);
    prefs_button(surface, prefs_box(PREFS_MODELS_BACK), "Back", PREFS_SECONDARY);
}

static void draw_password(const JelliPrefs *prefs, JelliSurface *surface)
{
    const JelliPrefsNetwork *network = &prefs->networks[prefs->chosen];
    prefs_name_centered(surface, 38, network->ssid, network->ssid_size, 14u, PREFS_WHITE);
    PrefsBox field = prefs_box(PREFS_PASS_FIELD);
    jelli_agent_round_outline(surface, field.x, field.y, field.width, field.height, 10, 2,
                              PREFS_LILAC);
    enum { SHOWN = 13 }; /* Characters that fit the field; the latest are shown. */
    size_t size = prefs->password_size, first = size > SHOWN ? size - SHOWN : 0u;
    char dots[SHOWN];
    memset(dots, '*', sizeof(dots));
    if (!size)
        jelli_agent_text(surface, field.x + 10, field.y + 9, "Password", 8u, 2u, PREFS_GREY);
    else
        jelli_agent_text(surface, field.x + 10, field.y + 9,
                         prefs->reveal ? prefs->password + first : dots, size - first, 2u,
                         PREFS_WHITE);
    prefs_button(surface, prefs_box(PREFS_PASS_SHOW), prefs->reveal ? "Hide" : "Show",
                 PREFS_SECONDARY);
    prefs_keyboard(surface, prefs);
    prefs_button(surface, prefs_box(PREFS_PASS_CANCEL), "Cancel", PREFS_SECONDARY);
}

static void draw_joining(const JelliPrefs *prefs, JelliSurface *surface)
{
    const JelliPrefsNetwork *network = &prefs->networks[prefs->chosen];
    title(surface, "Joining", 150);
    prefs_name_centered(surface, 214, network->ssid, network->ssid_size, 16u, PREFS_WHITE);
    waiting(surface, "", 262, prefs->frame);
}

/* Each outcome in words, two lines at most. */
static void outcome_words(JelliPrefsOutcome outcome, const char **first, const char **second)
{
    switch (outcome) {
    case JELLI_PREFS_JOINED:
        *first = "Connected";
        *second = "";
        break;
    case JELLI_PREFS_WRONG_PASSWORD:
        *first = "Wrong";
        *second = "password";
        break;
    case JELLI_PREFS_NOT_FOUND:
        *first = "Not found";
        *second = "";
        break;
    case JELLI_PREFS_UNSUPPORTED:
        *first = "Not";
        *second = "supported";
        break;
    case JELLI_PREFS_NOT_LOCKED:
        *first = "Not locked";
        *second = "Join anyway?";
        break;
    case JELLI_PREFS_FAILED:
        *first = "Couldn't";
        *second = "connect";
        break;
    }
}

static void draw_result(const JelliPrefs *prefs, JelliSurface *surface)
{
    JelliPrefsOutcome outcome = (JelliPrefsOutcome)prefs->outcome;
    const JelliPrefsNetwork *network = &prefs->networks[prefs->chosen];
    const char *first = "", *second = "";
    outcome_words(outcome, &first, &second);
    bool joined = outcome == JELLI_PREFS_JOINED;
    uint16_t color = joined ? PREFS_LILAC : PREFS_WARM;
    jelli_agent_text_centered(surface, PREFS_CENTER, 120, first, strlen(first), 3u, color);
    jelli_agent_text_centered(surface, PREFS_CENTER, 164, second, strlen(second), 3u, color);
    prefs_name_centered(surface, 226, network->ssid, network->ssid_size, 16u, PREFS_WHITE);
    if (outcome == JELLI_PREFS_UNSUPPORTED)
        jelli_agent_text_centered(surface, PREFS_CENTER, 262, "It needs a login", 16u, 2u,
                                  PREFS_GREY);
    if (outcome == JELLI_PREFS_NOT_LOCKED) { /* The safe way back is the bright one. */
        jelli_agent_text_centered(surface, PREFS_CENTER, 262, "It replaces a locked one", 24u, 2u,
                                  PREFS_GREY);
        prefs_button(surface, prefs_box(PREFS_RESULT_RETRY), "Join", PREFS_SECONDARY);
        prefs_button(surface, prefs_box(PREFS_RESULT_BACK), "Back", PREFS_PRIMARY);
        return;
    }
    if (joined || outcome == JELLI_PREFS_UNSUPPORTED) {
        prefs_button(surface, prefs_box(PREFS_RESULT_ALONE), joined ? "OK" : "Back",
                     joined ? PREFS_PRIMARY : PREFS_SECONDARY);
        return;
    }
    prefs_button(surface, prefs_box(PREFS_RESULT_RETRY), "Try again", PREFS_PRIMARY);
    prefs_button(surface, prefs_box(PREFS_RESULT_BACK), "Back", PREFS_SECONDARY);
}

void jelli_prefs_draw(JelliPrefs *prefs, JelliSurface *surface)
{
    if (!prefs || !surface || !surface->pixels)
        return;
    jelli_agent_fill(surface, 0, 0, (int)surface->width, (int)surface->height, PREFS_BLACK);
    switch (prefs->screen) {
    case JELLI_PREFS_MENU:
        draw_menu(prefs, surface);
        break;
    case JELLI_PREFS_SCANNING:
        draw_scanning(prefs, surface);
        break;
    case JELLI_PREFS_LIST:
        draw_list(prefs, surface);
        break;
    case JELLI_PREFS_PASSWORD:
        draw_password(prefs, surface);
        break;
    case JELLI_PREFS_JOINING:
        draw_joining(prefs, surface);
        break;
    case JELLI_PREFS_RESULT:
        draw_result(prefs, surface);
        break;
    case JELLI_PREFS_MODELS:
        draw_models(prefs, surface);
        break;
    case JELLI_PREFS_CLOSED:
        break;
    }
    surface->damage = (JelliRect){0u, 0u, surface->width, surface->height};
    prefs->dirty = false;
}
