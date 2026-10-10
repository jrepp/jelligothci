#include "jelli/agent_loki.h"
#include "prefs_internal.h"
#include <string.h>

/* The default model in Settings (ADR-018): the bridge offers a list of models with the
 * one in use, Settings shows it on the Model card and as a list, and a tap on another
 * picks it at once and asks the host to tell the bridge, whose next list confirms it. */

_Static_assert(JELLI_PREFS_MAX_MODELS == JELLI_LOKI_MODEL_MAX &&
                   JELLI_PREFS_MODEL_MAX + 1 == JELLI_LOKI_MODEL_NAME,
               "Settings holds every list loki/1 can send");

/* Whether models keeps loki/1's rules: at most 16 names, each a model name ending in
 * its buffer, and current among them. */
static bool valid_models(const JelliPrefsModels *models)
{
    if (models->count > JELLI_PREFS_MAX_MODELS ||
        (models->count && models->current >= models->count))
        return false;
    for (unsigned i = 0u; i < models->count; ++i) {
        const char *name = models->names[i];
        const char *end = memchr(name, '\0', sizeof(models->names[i]));
        if (!end || !jelli_loki_model_name(name, (size_t)(end - name)))
            return false;
    }
    return true;
}

/* The first row shown, so the default is in view: in the middle when it can be. */
static uint8_t top_for(unsigned count, unsigned current)
{
    unsigned last = count > PREFS_LIST_ROWS ? count - PREFS_LIST_ROWS : 0u;
    unsigned top = current > PREFS_LIST_ROWS / 2u ? current - PREFS_LIST_ROWS / 2u : 0u;
    return (uint8_t)(top < last ? top : last);
}

bool jelli_prefs_models(JelliPrefs *prefs, const JelliPrefsModels *models, bool online)
{
    if (!prefs || (models && !valid_models(models)))
        return false;
    if (prefs->screen == JELLI_PREFS_CLOSED)
        return true; /* Nothing shown; opening asks again. */
    memset(&prefs->models, 0, sizeof(prefs->models));
    if (models && models->count) {
        prefs->models.count = models->count;
        prefs->models.current = models->current;
        memcpy(prefs->models.names, models->names, sizeof(models->names[0]) * models->count);
    }
    prefs->bridge_online = online;
    unsigned last =
        prefs->models.count > PREFS_LIST_ROWS ? prefs->models.count - PREFS_LIST_ROWS : 0u;
    if (prefs->model_top > last)
        prefs->model_top = (uint8_t)last; /* A shorter list: still a full page. */
    if (prefs->screen == JELLI_PREFS_MODELS && !prefs->models.count)
        prefs_enter(prefs, JELLI_PREFS_MENU, prefs->since_ms);
    prefs->dirty = true;
    return true;
}

const char *jelli_prefs_model(const JelliPrefs *prefs)
{
    if (!prefs || !prefs->models.count || prefs->models.current >= prefs->models.count)
        return NULL;
    return prefs->models.names[prefs->models.current];
}

JelliPrefsAction prefs_open_models(JelliPrefs *prefs, uint64_t now_ms)
{
    if (!prefs->models.count)
        return JELLI_PREFS_DO_NOTHING; /* The card says why. */
    prefs->model_top = top_for(prefs->models.count, prefs->models.current);
    prefs_enter(prefs, JELLI_PREFS_MODELS, now_ms);
    return JELLI_PREFS_DO_NOTHING;
}

/* The picked model is outlined at once; the bridge's next list confirms it or not. */
JelliPrefsAction prefs_tap_models(JelliPrefs *prefs, int x, int y, uint64_t now_ms)
{
    if (prefs_inside(prefs_box(PREFS_MODELS_BACK), x, y)) {
        prefs_enter(prefs, JELLI_PREFS_MENU, now_ms);
        return JELLI_PREFS_DO_NOTHING;
    }
    for (unsigned row = 0u; row < PREFS_LIST_ROWS; ++row) {
        unsigned index = (unsigned)prefs->model_top + row;
        if (index >= prefs->models.count || !prefs_inside(prefs_row_box(row), x, y))
            continue;
        if (index == prefs->models.current)
            return JELLI_PREFS_DO_NOTHING; /* Already the default. */
        prefs->models.current = (uint8_t)index;
        prefs->dirty = true;
        return JELLI_PREFS_DO_SET_MODEL;
    }
    return JELLI_PREFS_DO_NOTHING;
}
