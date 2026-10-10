#ifndef JELLI_AGENT_PREFS_H
#define JELLI_AGENT_PREFS_H

#include "jelli/engine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Loki Agent's Settings (ADR-016): a menu showing the Wi-Fi network, the networks
 * nearby, a keyboard for a password, and joining; and the model the bridge answers
 * with by default, picked from the bridge's list (ADR-018). A state machine over taps,
 * drags, and the host's Wi-Fi results and model lists: it never touches the radio or
 * the link, but asks the host to scan, join, or send the model picked through the
 * action a tap returns, and draws itself into the host's 466x466 round surface. Fixed
 * storage only. The password lives here only while it is typed: a join copies it into
 * the host's request and wipes it here, and closing wipes it. */

enum {
    JELLI_PREFS_MAX_NETWORKS = 16,
    JELLI_PREFS_SSID_MAX = 32,
    JELLI_PREFS_PASSWORD_MIN = 8, /* A WPA passphrase: 8..63 printable ASCII. */
    JELLI_PREFS_PASSWORD_MAX = 63,
    JELLI_PREFS_RESULT_MS = 2000, /* A successful join shows this long, then Settings. */
    JELLI_PREFS_ANIMATE_MS = 400, /* "Searching..." and "Joining..." dots. */
    JELLI_PREFS_MAX_MODELS = 16,  /* As loki/1's models message (ADR-018). */
    JELLI_PREFS_MODEL_MAX = 40    /* Characters in a model name. */
};

typedef enum {
    JELLI_PREFS_CLOSED,
    JELLI_PREFS_MENU, /* Settings: the Wi-Fi network, and Done. */
    JELLI_PREFS_SCANNING,
    JELLI_PREFS_LIST, /* The networks nearby, or why there are none. */
    JELLI_PREFS_PASSWORD,
    JELLI_PREFS_JOINING,
    JELLI_PREFS_RESULT, /* How a join went. */
    JELLI_PREFS_MODELS  /* The models the bridge offers, the default outlined. */
} JelliPrefsScreen;

typedef enum {
    JELLI_PREFS_OPEN_NETWORK,
    JELLI_PREFS_PASSWORD_NETWORK,
    JELLI_PREFS_ENTERPRISE_NETWORK /* Needs a login the board cannot do. */
} JelliPrefsSecurity;

typedef struct {
    uint8_t ssid[JELLI_PREFS_SSID_MAX]; /* Raw bytes, as broadcast. */
    uint8_t ssid_size;                  /* 1..32; a hidden network (0) is never listed. */
    int8_t rssi;                        /* dBm. */
    uint8_t security;                   /* JelliPrefsSecurity. */
} JelliPrefsNetwork;

typedef enum {
    JELLI_PREFS_JOINED,
    JELLI_PREFS_WRONG_PASSWORD,
    JELLI_PREFS_NOT_FOUND,
    JELLI_PREFS_FAILED,
    JELLI_PREFS_UNSUPPORTED, /* An enterprise network: never asked of the host. */
    /* An open network, while the board's own has a password: anyone nearby can name
     * one to look like it. Asked about before joining; never asked of the host. */
    JELLI_PREFS_NOT_LOCKED
} JelliPrefsOutcome;

typedef enum {
    JELLI_PREFS_DO_NOTHING,
    JELLI_PREFS_DO_SCAN,  /* Scan, then call jelli_prefs_scanned. */
    JELLI_PREFS_DO_JOIN,  /* Join the request's network, then call jelli_prefs_joined. */
    JELLI_PREFS_DO_CLOSE, /* Settings closed itself (Done). */
    /* Tell the bridge the default is now the model jelli_prefs_model names (set_model). */
    JELLI_PREFS_DO_SET_MODEL
} JelliPrefsAction;

/* A join the host is asked to make. It owns this copy and wipes the password after. */
typedef struct {
    uint8_t ssid[JELLI_PREFS_SSID_MAX];
    uint8_t ssid_size;
    char password[JELLI_PREFS_PASSWORD_MAX + 1]; /* NUL-terminated; empty for open. */
    uint8_t password_size;
} JelliPrefsJoin;

/* The models the bridge offers as the default (loki/1's models). Each name is 1 to 40
 * characters of A-Z a-z 0-9 . _ : / + -, NUL-terminated; unused names are zero. */
typedef struct {
    char names[JELLI_PREFS_MAX_MODELS][JELLI_PREFS_MODEL_MAX + 1];
    uint8_t count;   /* 0 when no list has come. */
    uint8_t current; /* The default, below count. */
} JelliPrefsModels;

typedef struct {
    JelliPrefsScreen screen;
    uint8_t current[JELLI_PREFS_SSID_MAX]; /* The network the board uses, if any. */
    uint8_t current_size;
    bool current_secured; /* That network has a password. */
    bool connected, scan_failed;
    JelliPrefsNetwork networks[JELLI_PREFS_MAX_NETWORKS]; /* Strongest first. */
    uint8_t count, top; /* Networks listed, and the first one shown. */
    uint8_t chosen;     /* The network being joined, an index into networks. */
    char password[JELLI_PREFS_PASSWORD_MAX + 1];
    uint8_t password_size, page; /* The keyboard's page (letters, capitals, ...). */
    bool reveal;                 /* The password shown as typed, not as dots. */
    uint8_t outcome;             /* JelliPrefsOutcome of the last join. */
    JelliPrefsModels models;     /* The bridge's list, with the user's pick as current. */
    uint8_t model_top;           /* The first model shown. */
    bool bridge_online;          /* Without a list: online, the bridge offers none. */
    int drag;                    /* Vertical drag not yet turned into list rows. */
    uint64_t since_ms;           /* When this screen was entered. */
    unsigned frame;              /* Animation step drawn last. */
    bool dirty;                  /* Changed since it was last drawn. */
} JelliPrefs;

void jelli_prefs_init(JelliPrefs *prefs);

/* Opens Settings showing the network the board uses (ssid_size 0 for none). */
void jelli_prefs_open(JelliPrefs *prefs, const uint8_t *ssid, size_t ssid_size, bool connected,
                      uint64_t now_ms);

/* After opening: whether the board's network has a password. A network listed under
 * its name counts as the board's only when it is locked the same way, and while it has
 * a password any open network is joined only after a warning: a look-alike's name can
 * differ from the real one's by what the screen cannot show. */
void jelli_prefs_secured(JelliPrefs *prefs, bool secured);

/* Closes Settings and wipes the password, the networks, and the models. */
void jelli_prefs_close(JelliPrefs *prefs);

/* After opening, and whenever they change: the models the bridge offers (NULL or a
 * count of 0 when none have come) and whether the bridge is online. The list replaces
 * the one shown, a pick not yet confirmed included; without one, the Model card says
 * why and does not open, and an open list goes back to Settings. Ignored while
 * closed, and refused (false, nothing changed) when the list breaks loki/1's rules. */
bool jelli_prefs_models(JelliPrefs *prefs, const JelliPrefsModels *models, bool online);

/* The default model's name: for DO_SET_MODEL, the one just picked. NULL without a list. */
const char *jelli_prefs_model(const JelliPrefs *prefs);

/* A tap at (x, y) on the surface. For DO_JOIN it fills join (else leaves it alone). */
JelliPrefsAction jelli_prefs_tap(JelliPrefs *prefs, int x, int y, uint64_t now_ms,
                                 JelliPrefsJoin *join);

/* A vertical drag (down positive) scrolls the networks or the models a row at a time. */
void jelli_prefs_drag(JelliPrefs *prefs, int dy);

/* What a scan found (ok false when it failed). Ignored unless Searching is shown:
 * hidden networks go, each name keeps its strongest signal (an open and a locked
 * network of one name are listed apart), strongest first. */
void jelli_prefs_scanned(JelliPrefs *prefs, const JelliPrefsNetwork *found, size_t count, bool ok,
                         uint64_t now_ms);

/* How the requested join went (JOINED to FAILED); ignored unless Joining is shown. */
void jelli_prefs_joined(JelliPrefs *prefs, JelliPrefsOutcome outcome, uint64_t now_ms);

/* Animates and moves on from a shown success; true when there is something to draw. */
bool jelli_prefs_tick(JelliPrefs *prefs, uint64_t now_ms);

/* Draws the whole screen (damage: the whole surface) and clears dirty. */
void jelli_prefs_draw(JelliPrefs *prefs, JelliSurface *surface);

#endif
