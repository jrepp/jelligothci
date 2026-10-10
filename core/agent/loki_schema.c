#include "loki_schema.h"
#include <string.h>

#define FIELD(name, member, kind, required, min, max)                                              \
    {name, JELLI_LOKI_FIELD_##kind, required, offsetof(JelliLokiMessage, member), min, max}
#define MEMBER_HOLDS(member, bytes) (sizeof(((JelliLokiMessage *)0)->member) > (bytes))

_Static_assert(MEMBER_HOLDS(bridge, 40) && MEMBER_HOLDS(voice, 40) && MEMBER_HOLDS(text, 400) &&
                   MEMBER_HOLDS(id, 32) && MEMBER_HOLDS(nonce, 32) && MEMBER_HOLDS(title, 120) &&
                   MEMBER_HOLDS(detail, 600) && MEMBER_HOLDS(options[0], 40) &&
                   MEMBER_HOLDS(models[0], 40),
               "every string member holds its field's longest value and a NUL");

static const JelliLokiField WELCOME[] = {FIELD("proto", proto, U32, true, 1, 1),
                                         FIELD("bridge", bridge, LINE, true, 0, 40),
                                         FIELD("voice", voice, LINE, false, 0, 40)};
static const JelliLokiField STATE[] = {
    FIELD("state", state, STATE, true, 0, 0), FIELD("caption", text, TEXT, false, 0, 200),
    FIELD("progress", progress, I32, false, -1, 1000), FIELD("queue", queue, U32, false, 0, 999)};
static const JelliLokiField CAPTION[] = {FIELD("text", text, TEXT, true, 0, 400),
                                         FIELD("final", final, BOOL, true, 0, 0)};
static const JelliLokiField ASK[] = {FIELD("id", id, ID, true, 1, 32),
                                     FIELD("nonce", nonce, HEX, true, 32, 32),
                                     FIELD("kind", kind, KIND, true, 0, 0),
                                     FIELD("title", title, LINE, true, 0, 120),
                                     FIELD("detail", detail, TEXT, false, 0, 600),
                                     FIELD("options", options, OPTIONS, true, 2, 4),
                                     FIELD("timeout_ms", timeout_ms, U32, true, 1000, 600000)};
static const JelliLokiField ASK_CANCEL[] = {FIELD("id", id, ID, true, 1, 32)};
static const JelliLokiField REPLY_BEGIN[] = {
    FIELD("reply", reply, U32, true, 1, UINT32_MAX),
    FIELD("rate", rate, U32, true, JELLI_LOKI_RATE, JELLI_LOKI_RATE)};
static const JelliLokiField REPLY_END[] = {FIELD("reply", reply, U32, true, 1, UINT32_MAX),
                                           FIELD("frames", frames, U32, true, 0, UINT32_MAX)};
static const JelliLokiField SETTINGS[] = {FIELD("volume", volume, U32, true, 0, 100)};
/* current must also lie below the count (loki.c checks it once both are read). */
static const JelliLokiField MODELS[] = {
    FIELD("models", models, MODELS, true, 1, JELLI_LOKI_MODEL_MAX),
    FIELD("current", current, U32, true, 0, JELLI_LOKI_MODEL_MAX - 1)};

#define LIST(items, count, item)                                                                   \
    {offsetof(JelliLokiMessage, items), offsetof(JelliLokiMessage, count),                         \
     sizeof(((JelliLokiMessage *)0)->items[0]),                                                    \
     sizeof(((JelliLokiMessage *)0)->items) / sizeof(((JelliLokiMessage *)0)->items[0]),           \
     JELLI_LOKI_FIELD_##item}
static const JelliLokiList OPTION_LIST = LIST(options, option_count, LINE);
static const JelliLokiList MODEL_LIST = LIST(models, model_count, MODEL);
_Static_assert(sizeof(((JelliLokiMessage *)0)->option_count) == sizeof(unsigned) &&
                   sizeof(((JelliLokiMessage *)0)->model_count) == sizeof(unsigned),
               "list counts are unsigned");

#define TYPE(name, type, fields) {name, fields, type, sizeof(fields) / sizeof((fields)[0])}
static const JelliLokiTypeSpec TYPES[] = {TYPE("welcome", JELLI_LOKI_WELCOME, WELCOME),
                                          TYPE("state", JELLI_LOKI_STATE, STATE),
                                          TYPE("caption", JELLI_LOKI_CAPTION, CAPTION),
                                          TYPE("ask", JELLI_LOKI_ASK, ASK),
                                          TYPE("ask_cancel", JELLI_LOKI_ASK_CANCEL, ASK_CANCEL),
                                          TYPE("reply_begin", JELLI_LOKI_REPLY_BEGIN, REPLY_BEGIN),
                                          TYPE("reply_end", JELLI_LOKI_REPLY_END, REPLY_END),
                                          {"stop_audio", NULL, JELLI_LOKI_STOP_AUDIO, 0u},
                                          TYPE("settings", JELLI_LOKI_SETTINGS, SETTINGS),
                                          TYPE("models", JELLI_LOKI_MODELS, MODELS)};

static const char *const STATES[] = {"idle",   "listening", "heard",   "thinking", "working",
                                     "asking", "speaking",  "success", "partial",  "failure"};
static const char *const KINDS[] = {"approve", "choose"};

const JelliLokiTypeSpec *jelli_loki_type_named(const char *name)
{
    for (size_t i = 0u; i < sizeof(TYPES) / sizeof(TYPES[0]); ++i)
        if (!strcmp(name, TYPES[i].name))
            return &TYPES[i];
    return NULL;
}

const JelliLokiTypeSpec *jelli_loki_type_of(JelliLokiType type)
{
    for (size_t i = 0u; i < sizeof(TYPES) / sizeof(TYPES[0]); ++i)
        if (TYPES[i].type == type)
            return &TYPES[i];
    return NULL;
}

const JelliLokiList *jelli_loki_list(JelliLokiFieldKind kind)
{
    return kind == JELLI_LOKI_FIELD_OPTIONS  ? &OPTION_LIST
           : kind == JELLI_LOKI_FIELD_MODELS ? &MODEL_LIST
                                             : NULL;
}

bool jelli_loki_id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

bool jelli_loki_model_name(const char *text, size_t length)
{
    if (!text || length < 1u || length > JELLI_LOKI_MODEL_NAME - 1u)
        return false;
    for (size_t i = 0u; i < length; ++i)
        if (!jelli_loki_id_char(text[i]) && text[i] != '.' && text[i] != ':' && text[i] != '/' &&
            text[i] != '+')
            return false;
    return true;
}

static int index_of(const char *const *words, size_t count, const char *word)
{
    for (size_t i = 0u; i < count; ++i)
        if (!strcmp(word, words[i]))
            return (int)i;
    return -1;
}

const char *jelli_loki_state_name(unsigned state)
{
    return state < sizeof(STATES) / sizeof(STATES[0]) ? STATES[state] : "?";
}

const char *jelli_loki_kind_name(unsigned kind)
{
    return kind < sizeof(KINDS) / sizeof(KINDS[0]) ? KINDS[kind] : "?";
}

int jelli_loki_state_index(const char *word)
{
    return index_of(STATES, sizeof(STATES) / sizeof(STATES[0]), word);
}

int jelli_loki_kind_index(const char *word)
{
    return index_of(KINDS, sizeof(KINDS) / sizeof(KINDS[0]), word);
}

const JelliLokiField *jelli_loki_card_field(unsigned index)
{
    static const unsigned CARD[JELLI_LOKI_CARD_FIELDS] = {2u, 3u, 4u, 5u};
    return index < JELLI_LOKI_CARD_FIELDS ? &ASK[CARD[index]] : NULL;
}
