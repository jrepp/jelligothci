#include "prefs_internal.h"
#include <string.h>

static const PrefsBox BOXES[PREFS_PART_COUNT] = {
    [PREFS_MENU_WIFI] = {73, 112, 320, 80},     [PREFS_MENU_MODEL] = {73, 204, 320, 80},
    [PREFS_MENU_DONE] = {153, 300, 160, 56},    [PREFS_MODELS_BACK] = {173, 360, 120, 48},
    [PREFS_SCAN_BACK] = {163, 320, 140, 52},    [PREFS_LIST_BACK] = {103, 360, 120, 48},
    [PREFS_LIST_SCAN] = {243, 360, 120, 48},    [PREFS_PASS_FIELD] = {78, 72, 236, 42},
    [PREFS_PASS_SHOW] = {320, 72, 70, 42},      [PREFS_PASS_CANCEL] = {163, 318, 140, 44},
    [PREFS_RESULT_RETRY] = {78, 320, 150, 52},  [PREFS_RESULT_BACK] = {238, 320, 150, 52},
    [PREFS_RESULT_ALONE] = {163, 320, 140, 52},
};

PrefsBox prefs_box(PrefsPart part)
{
    return part < PREFS_PART_COUNT ? BOXES[part] : (PrefsBox){0, 0, 0, 0};
}

PrefsBox prefs_row_box(unsigned row)
{
    return (PrefsBox){PREFS_LIST_X, PREFS_LIST_Y + (int)row * PREFS_ROW_PITCH, PREFS_LIST_WIDTH,
                      PREFS_ROW_HEIGHT};
}

bool prefs_inside(PrefsBox box, int x, int y)
{
    return x >= box.x && y >= box.y && x - box.x < box.width && y - box.y < box.height;
}

void jelli_prefs_init(JelliPrefs *prefs)
{
    if (prefs) {
        *prefs = (JelliPrefs){0};
        prefs_wipe(prefs);
    }
}

void prefs_enter(JelliPrefs *prefs, JelliPrefsScreen screen, uint64_t now_ms)
{
    prefs->screen = screen;
    prefs->since_ms = now_ms;
    prefs->frame = 0u;
    prefs->drag = 0;
    prefs->dirty = true;
}

void jelli_prefs_open(JelliPrefs *prefs, const uint8_t *ssid, size_t ssid_size, bool connected,
                      uint64_t now_ms)
{
    if (!prefs)
        return;
    jelli_prefs_close(prefs);
    if (ssid && ssid_size && ssid_size <= JELLI_PREFS_SSID_MAX) {
        memcpy(prefs->current, ssid, ssid_size);
        prefs->current_size = (uint8_t)ssid_size;
    }
    prefs->connected = connected && prefs->current_size;
    prefs_enter(prefs, JELLI_PREFS_MENU, now_ms);
}

void jelli_prefs_secured(JelliPrefs *prefs, bool secured)
{
    if (prefs) {
        prefs->current_secured = secured && prefs->current_size;
        prefs->dirty = true;
    }
}

/* Whether joining network would replace the board's locked network with an open one.
 * Any name counts: a look-alike can differ by a trailing space or by bytes the screen
 * draws as '?', so names are not compared. */
static bool replaces_locked(const JelliPrefs *prefs, const JelliPrefsNetwork *network)
{
    return prefs->current_secured && network->security == JELLI_PREFS_OPEN_NETWORK;
}

void jelli_prefs_close(JelliPrefs *prefs)
{
    if (!prefs)
        return;
    prefs_wipe(prefs);
    memset(prefs->networks, 0, sizeof(prefs->networks));
    memset(prefs->current, 0, sizeof(prefs->current));
    memset(&prefs->models, 0, sizeof(prefs->models));
    prefs->count = prefs->top = prefs->chosen = prefs->current_size = prefs->model_top = 0u;
    prefs->connected = prefs->scan_failed = prefs->current_secured = false;
    prefs->bridge_online = false;
    prefs->screen = JELLI_PREFS_CLOSED;
    prefs->dirty = false;
}

/* Hands the chosen network (and the typed password) to the host and wipes it here. */
static JelliPrefsAction join(JelliPrefs *prefs, uint64_t now_ms, JelliPrefsJoin *request)
{
    const JelliPrefsNetwork *network = &prefs->networks[prefs->chosen];
    if (!request)
        return JELLI_PREFS_DO_NOTHING;
    memset(request, 0, sizeof(*request));
    memcpy(request->ssid, network->ssid, network->ssid_size);
    request->ssid_size = network->ssid_size;
    if (network->security == JELLI_PREFS_PASSWORD_NETWORK) {
        memcpy(request->password, prefs->password, prefs->password_size);
        request->password_size = prefs->password_size;
    }
    prefs_wipe(prefs);
    prefs_enter(prefs, JELLI_PREFS_JOINING, now_ms);
    return JELLI_PREFS_DO_JOIN;
}

static void show_outcome(JelliPrefs *prefs, JelliPrefsOutcome outcome, uint64_t now_ms)
{
    prefs->outcome = (uint8_t)outcome;
    prefs_enter(prefs, JELLI_PREFS_RESULT, now_ms);
}

/* Asks for a password, joins an open network at once, and refuses an enterprise one. */
static JelliPrefsAction choose(JelliPrefs *prefs, unsigned index, uint64_t now_ms,
                               JelliPrefsJoin *request)
{
    prefs->chosen = (uint8_t)index;
    switch (prefs->networks[index].security) {
    case JELLI_PREFS_OPEN_NETWORK:
        if (!replaces_locked(prefs, &prefs->networks[index]))
            return join(prefs, now_ms, request);
        show_outcome(prefs, JELLI_PREFS_NOT_LOCKED, now_ms); /* Asked about first. */
        return JELLI_PREFS_DO_NOTHING;
    case JELLI_PREFS_PASSWORD_NETWORK:
        prefs_wipe(prefs);
        prefs->page = PREFS_LOWER;
        prefs_enter(prefs, JELLI_PREFS_PASSWORD, now_ms);
        return JELLI_PREFS_DO_NOTHING;
    default:
        show_outcome(prefs, JELLI_PREFS_UNSUPPORTED, now_ms);
        return JELLI_PREFS_DO_NOTHING;
    }
}

static JelliPrefsAction scan(JelliPrefs *prefs, uint64_t now_ms)
{
    prefs_enter(prefs, JELLI_PREFS_SCANNING, now_ms);
    return JELLI_PREFS_DO_SCAN;
}

static JelliPrefsAction tap_menu(JelliPrefs *prefs, int x, int y, uint64_t now_ms)
{
    if (prefs_inside(prefs_box(PREFS_MENU_WIFI), x, y))
        return scan(prefs, now_ms);
    if (prefs_inside(prefs_box(PREFS_MENU_MODEL), x, y))
        return prefs_open_models(prefs, now_ms);
    if (!prefs_inside(prefs_box(PREFS_MENU_DONE), x, y))
        return JELLI_PREFS_DO_NOTHING;
    jelli_prefs_close(prefs);
    return JELLI_PREFS_DO_CLOSE;
}

static JelliPrefsAction tap_list(JelliPrefs *prefs, int x, int y, uint64_t now_ms,
                                 JelliPrefsJoin *request)
{
    if (prefs_inside(prefs_box(PREFS_LIST_BACK), x, y))
        prefs_enter(prefs, JELLI_PREFS_MENU, now_ms);
    else if (prefs_inside(prefs_box(PREFS_LIST_SCAN), x, y))
        return scan(prefs, now_ms);
    for (unsigned row = 0u; row < PREFS_LIST_ROWS && prefs->screen == JELLI_PREFS_LIST; ++row) {
        unsigned index = (unsigned)prefs->top + row;
        if (index < prefs->count && prefs_inside(prefs_row_box(row), x, y))
            return choose(prefs, index, now_ms, request);
    }
    return JELLI_PREFS_DO_NOTHING;
}

static JelliPrefsAction tap_password(JelliPrefs *prefs, int x, int y, uint64_t now_ms,
                                     JelliPrefsJoin *request)
{
    PrefsKey key;
    if (prefs_inside(prefs_box(PREFS_PASS_CANCEL), x, y)) {
        prefs_wipe(prefs);
        prefs_enter(prefs, JELLI_PREFS_LIST, now_ms);
    } else if (prefs_inside(prefs_box(PREFS_PASS_SHOW), x, y)) {
        prefs->reveal = !prefs->reveal;
        prefs->dirty = true;
    } else if (prefs_key_at(prefs->page, x, y, &key) && prefs_type(prefs, &key)) {
        return join(prefs, now_ms, request);
    }
    return JELLI_PREFS_DO_NOTHING;
}

/* After a failed join: an open network is tried again, else the password is asked anew. */
static JelliPrefsAction tap_result(JelliPrefs *prefs, int x, int y, uint64_t now_ms,
                                   JelliPrefsJoin *request)
{
    JelliPrefsOutcome outcome = (JelliPrefsOutcome)prefs->outcome;
    if (outcome == JELLI_PREFS_JOINED) {
        prefs_enter(prefs, JELLI_PREFS_MENU, now_ms);
    } else if (outcome == JELLI_PREFS_NOT_LOCKED) {
        if (prefs_inside(prefs_box(PREFS_RESULT_RETRY), x, y))
            return join(prefs, now_ms, request); /* Joined anyway, knowingly. */
        if (prefs_inside(prefs_box(PREFS_RESULT_BACK), x, y))
            prefs_enter(prefs, JELLI_PREFS_LIST, now_ms);
    } else if (outcome == JELLI_PREFS_UNSUPPORTED) {
        if (prefs_inside(prefs_box(PREFS_RESULT_ALONE), x, y))
            prefs_enter(prefs, JELLI_PREFS_LIST, now_ms);
    } else if (prefs_inside(prefs_box(PREFS_RESULT_RETRY), x, y)) {
        return choose(prefs, prefs->chosen, now_ms, request);
    } else if (prefs_inside(prefs_box(PREFS_RESULT_BACK), x, y)) {
        prefs_enter(prefs, JELLI_PREFS_LIST, now_ms);
    }
    return JELLI_PREFS_DO_NOTHING;
}

JelliPrefsAction jelli_prefs_tap(JelliPrefs *prefs, int x, int y, uint64_t now_ms,
                                 JelliPrefsJoin *join_request)
{
    if (!prefs)
        return JELLI_PREFS_DO_NOTHING;
    switch (prefs->screen) {
    case JELLI_PREFS_MENU:
        return tap_menu(prefs, x, y, now_ms);
    case JELLI_PREFS_SCANNING:
        if (prefs_inside(prefs_box(PREFS_SCAN_BACK), x, y))
            prefs_enter(prefs, JELLI_PREFS_MENU, now_ms);
        return JELLI_PREFS_DO_NOTHING;
    case JELLI_PREFS_LIST:
        return tap_list(prefs, x, y, now_ms, join_request);
    case JELLI_PREFS_PASSWORD:
        return tap_password(prefs, x, y, now_ms, join_request);
    case JELLI_PREFS_RESULT:
        return tap_result(prefs, x, y, now_ms, join_request);
    case JELLI_PREFS_MODELS:
        return prefs_tap_models(prefs, x, y, now_ms);
    case JELLI_PREFS_CLOSED:
    case JELLI_PREFS_JOINING:
        return JELLI_PREFS_DO_NOTHING;
    }
    return JELLI_PREFS_DO_NOTHING;
}

/* Scrolls a list of count rows whose first shown is *top. */
static void scroll(JelliPrefs *prefs, uint8_t *top, unsigned count, int dy)
{
    prefs->drag += dy;
    unsigned last = count > PREFS_LIST_ROWS ? count - PREFS_LIST_ROWS : 0u;
    /* A row per row's height of finger movement, so the list follows the finger. */
    while (prefs->drag >= PREFS_ROW_PITCH || prefs->drag <= -PREFS_ROW_PITCH) {
        bool down = prefs->drag > 0; /* A finger moving down shows the rows above. */
        if (down ? *top == 0u : *top >= last) {
            prefs->drag = 0; /* At the end: no drag builds up beyond it. */
            break;
        }
        *top = (uint8_t)(down ? *top - 1u : *top + 1u);
        prefs->drag += down ? -PREFS_ROW_PITCH : PREFS_ROW_PITCH;
        prefs->dirty = true;
    }
}

void jelli_prefs_drag(JelliPrefs *prefs, int dy)
{
    if (!prefs || dy < -4096 || dy > 4096)
        return;
    if (prefs->screen == JELLI_PREFS_LIST)
        scroll(prefs, &prefs->top, prefs->count, dy);
    else if (prefs->screen == JELLI_PREFS_MODELS)
        scroll(prefs, &prefs->model_top, prefs->models.count, dy);
}

/* Where a network with this name and security already is in the list; count when it
 * is not. An open and a locked network of one name stay apart, so a look-alike cannot
 * take the locked one's row. */
static unsigned find(const JelliPrefs *prefs, const JelliPrefsNetwork *network)
{
    for (unsigned i = 0u; i < prefs->count; ++i)
        if (prefs->networks[i].ssid_size == network->ssid_size &&
            prefs->networks[i].security == network->security &&
            !memcmp(prefs->networks[i].ssid, network->ssid, network->ssid_size))
            return i;
    return prefs->count;
}

/* Keeps each name once, at its strongest signal, and only the strongest names. */
static void keep(JelliPrefs *prefs, const JelliPrefsNetwork *network)
{
    if (!network->ssid_size || network->ssid_size > JELLI_PREFS_SSID_MAX)
        return;
    unsigned at = find(prefs, network);
    if (at < prefs->count) {
        if (network->rssi > prefs->networks[at].rssi)
            prefs->networks[at] = *network;
        return;
    }
    if (prefs->count < JELLI_PREFS_MAX_NETWORKS) {
        prefs->networks[prefs->count++] = *network;
        return;
    }
    unsigned weakest = 0u;
    for (unsigned i = 1u; i < prefs->count; ++i)
        if (prefs->networks[i].rssi < prefs->networks[weakest].rssi)
            weakest = i;
    if (network->rssi > prefs->networks[weakest].rssi)
        prefs->networks[weakest] = *network;
}

static void strongest_first(JelliPrefs *prefs)
{
    for (unsigned i = 1u; i < prefs->count; ++i) {
        JelliPrefsNetwork network = prefs->networks[i];
        unsigned j = i;
        for (; j > 0u && prefs->networks[j - 1u].rssi < network.rssi; --j)
            prefs->networks[j] = prefs->networks[j - 1u];
        prefs->networks[j] = network;
    }
}

void jelli_prefs_scanned(JelliPrefs *prefs, const JelliPrefsNetwork *found, size_t count, bool ok,
                         uint64_t now_ms)
{
    if (!prefs || prefs->screen != JELLI_PREFS_SCANNING)
        return;
    memset(prefs->networks, 0, sizeof(prefs->networks));
    prefs->count = prefs->top = prefs->chosen = 0u;
    prefs->scan_failed = !ok;
    for (size_t i = 0u; ok && found && i < count; ++i)
        keep(prefs, &found[i]);
    strongest_first(prefs);
    prefs_enter(prefs, JELLI_PREFS_LIST, now_ms);
}

void jelli_prefs_joined(JelliPrefs *prefs, JelliPrefsOutcome outcome, uint64_t now_ms)
{
    if (!prefs || prefs->screen != JELLI_PREFS_JOINING || outcome > JELLI_PREFS_FAILED)
        return;
    if (outcome == JELLI_PREFS_JOINED) {
        const JelliPrefsNetwork *network = &prefs->networks[prefs->chosen];
        memset(prefs->current, 0, sizeof(prefs->current));
        memcpy(prefs->current, network->ssid, network->ssid_size);
        prefs->current_size = network->ssid_size;
        prefs->current_secured = network->security != JELLI_PREFS_OPEN_NETWORK;
        prefs->connected = true;
    }
    show_outcome(prefs, outcome, now_ms);
}

bool jelli_prefs_tick(JelliPrefs *prefs, uint64_t now_ms)
{
    if (!prefs || prefs->screen == JELLI_PREFS_CLOSED)
        return false;
    uint64_t shown = now_ms >= prefs->since_ms ? now_ms - prefs->since_ms : 0u;
    if (prefs->screen == JELLI_PREFS_RESULT && prefs->outcome == JELLI_PREFS_JOINED &&
        shown >= JELLI_PREFS_RESULT_MS)
        prefs_enter(prefs, JELLI_PREFS_MENU, now_ms);
    bool animated = prefs->screen == JELLI_PREFS_SCANNING || prefs->screen == JELLI_PREFS_JOINING;
    unsigned frame = (unsigned)((shown / JELLI_PREFS_ANIMATE_MS) % 4u);
    if (animated && frame != prefs->frame) {
        prefs->frame = frame;
        prefs->dirty = true;
    }
    return prefs->dirty;
}
