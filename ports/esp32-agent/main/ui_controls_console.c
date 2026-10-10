#include "ui_controls.h"
#include "ui.h"
#include "ui_controls_internal.h"
#include "voice.h"
#include <stdio.h>
#include <string.h>

/* The controls' console commands (ADR-016, ADR-017): "screen sleep|wake|status" and the
 * "ui" asks that stand in for the button and touches. Console task; the UI task does
 * the work. */

/* In JelliPrefsScreen's order. */
static const char *view_name(int view)
{
    static const char *const names[] = {"closed",   "menu",    "searching", "networks",
                                        "password", "joining", "result",    "models"};
    return view >= 0 && view < (int)(sizeof(names) / sizeof(names[0])) ? names[view] : "closed";
}

static bool screen_command(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *what = request->count == 2u ? request->words[1] : "";
    bool ok = !strcmp(what, "status") ||
              (!strcmp(what, "sleep") && jelli_agent_ui_controls_ask(ASK_SLEEP, 0, 0)) ||
              (!strcmp(what, "wake") && jelli_agent_ui_controls_ask(ASK_WAKE, 0, 0));
    bool dark = false;
    int view = 0, networks = 0, models = 0;
    bool started = jelli_agent_ui_controls_status(&dark, &view, &networks, &models);
    if (!ok)
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"%s\"}",
                       started ? "syntax" : "unavailable");
    else
        (void)snprintf(out, capacity,
                       "{\"ok\":true,\"asleep\":%s,\"settings\":\"%s\",\"networks\":%d,"
                       "\"models\":%d,\"idle_ms\":%u,\"listening\":%s,\"ui_stack_free\":%u}",
                       dark ? "true" : "false", view_name(view), networks, models,
                       (unsigned)JELLI_AGENT_SCREEN_IDLE_MS,
                       jelli_agent_voice_holding() ? "true" : "false", jelli_agent_ui_stack_free());
    return true;
}

/* "ui button", "ui pet", and "ui hold start|stop": the asks named by words alone. */
static bool word_ask(const JelliAgentRequest *request, Ask *ask)
{
    const char *what = request->words[1], *how = request->count == 3u ? request->words[2] : "";
    bool alone = request->count == 2u, held = request->count == 3u && !strcmp(what, "hold");
    if (alone && !strcmp(what, "button"))
        ask->kind = ASK_BUTTON;
    else if (alone && !strcmp(what, "pet"))
        ask->kind = ASK_PET;
    else if (held && !strcmp(how, "start"))
        ask->kind = ASK_HOLD;
    else if (held && !strcmp(how, "stop"))
        ask->kind = ASK_RELEASE;
    else
        return false;
    return true;
}

/* "ui tap X Y" and "ui drag up|down PX": the asks with a place or a distance. */
static bool point_ask(const JelliAgentRequest *request, Ask *ask)
{
    uint32_t x = 0u, y = 0u;
    if (request->count != 4u)
        return false;
    const char *what = request->words[1], *way = request->words[2];
    if (!strcmp(what, "tap") && jelli_agent_number(request->words[2], &x) &&
        jelli_agent_number(request->words[3], &y) && x < 466u && y < 466u) {
        *ask = (Ask){ASK_TAP, (int)x, (int)y};
        return true;
    }
    bool up = !strcmp(way, "up");
    if (!strcmp(what, "drag") && (up || !strcmp(way, "down")) &&
        jelli_agent_number(request->words[3], &y) && y <= 466u) {
        *ask = (Ask){ASK_DRAG, 0, up ? -(int)y : (int)y};
        return true;
    }
    return false;
}

bool jelli_agent_command_controls(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->words[0];
    if (!strcmp(verb, "screen"))
        return screen_command(request, out, capacity);
    Ask ask = {ASK_BUTTON, 0, 0};
    if (strcmp(verb, "ui") != 0 || request->count < 2u ||
        (!word_ask(request, &ask) && !point_ask(request, &ask)))
        return false; /* Other "ui" verbs: the unicorn's own commands. */
    bool sent = jelli_agent_ui_controls_ask((AskKind)ask.kind, ask.x, ask.y);
    (void)snprintf(out, capacity, sent ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"busy\"}");
    return true;
}
