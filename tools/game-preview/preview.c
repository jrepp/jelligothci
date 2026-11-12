/* Jelli Art "Test in game": renders a scenario through the real pet renderer.
 *
 * Builds a JelliGame and JelliPetUi from bounded integer options, applies the
 * scenario through real commands where one exists (REST, MOMENT), renders the
 * requested frames with jelli_pet_render at injected times and writes each as
 * a binary PPM. One JSON line on stdout reports what the engine accepted. */
#include "jelli/activities.h"
#include "jelli/behavior.h"
#include "jelli/collection.h"
#include "jelli/locations.h"
#include "jelli/pet_ui.h"
#include "jelli/pet_scene.h"
#include "jelli/pet_effect.h"
#include <errno.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_FRAME_LIMIT 48u
#define PREVIEW_ADVANCE_LIMIT_S 3600u
#define PREVIEW_STEP_LIMIT_MS 10000u
#define PREVIEW_START_LIMIT_MS 86400000u
#define PREVIEW_PATH_CAPACITY 1024u

typedef struct {
    long entry, form, page, menu, behavior, potty, mess, asleep, moment, minute;
    long start_ms, step_ms, frames, advance_s, live;
    long location, present, present_state, completion;
    long tilt_x, tilt_y, travel_speed;
    long needs[JELLI_NEED_COUNT];
    const char *out, *scene, *effect;
} Scenario;

typedef struct {
    const char *name;
    size_t offset;
    long low, high;
} Option;

#define OPTION(field, low, high) {"--" #field, offsetof(Scenario, field), low, high}
static const Option options[] = {
    OPTION(tilt_x, -100, 100),
    OPTION(tilt_y, -100, 100),
    OPTION(travel_speed, -32, 32),
    OPTION(entry, 1, UINT8_MAX),
    OPTION(location, 0, JELLI_LOCATION_CAPACITY - 1),
    OPTION(present, 0, JELLI_PRIZE_COUNT),
    OPTION(present_state, 0, 2),
    OPTION(completion, 0, 2),
    OPTION(form, 0, JELLI_FORM_CAPACITY - 1),
    OPTION(page, 0, JELLI_UI_PAGE_COUNT - 1),
    OPTION(menu, 0, 1),
    OPTION(behavior, 0, JELLI_BEHAVIOR_STATE_CAPACITY),
    OPTION(potty, 0, 1000),
    OPTION(mess, 0, 1),
    OPTION(asleep, 0, 1),
    OPTION(moment, 0, JELLI_MOMENT_CAPACITY),
    OPTION(minute, -1, 1439),
    OPTION(start_ms, 0, PREVIEW_START_LIMIT_MS),
    OPTION(step_ms, 1, PREVIEW_STEP_LIMIT_MS),
    OPTION(frames, 1, PREVIEW_FRAME_LIMIT),
    OPTION(advance_s, 0, PREVIEW_ADVANCE_LIMIT_S),
    OPTION(live, 0, 1),
};
#undef OPTION

/* Static: the surface and UI state are too large for a small stack. */
static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
static uint8_t row[JELLI_WIDTH * 3u];
static JelliGame game;
static JelliPetUi ui;
static JelliEventLog events;
static JelliScene scene;

static bool parse_long(const char *text, long low, long high, long *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < low || parsed > high)
        return false;
    *value = parsed;
    return true;
}

static bool parse_needs(const char *text, Scenario *scenario)
{
    char buffer[64];
    size_t length = strlen(text);
    if (length >= sizeof(buffer))
        return false;
    memcpy(buffer, text, length + 1u);
    char *cursor = buffer;
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i) {
        char *comma = strchr(cursor, ',');
        if ((comma == NULL) != (i + 1u == JELLI_NEED_COUNT))
            return false;
        if (comma)
            *comma = '\0';
        if (!parse_long(cursor, 0, 1000, &scenario->needs[i]))
            return false;
        cursor = comma ? comma + 1 : cursor;
    }
    return true;
}

static bool parse_option(const char *name, const char *value, Scenario *scenario)
{
    if (strcmp(name, "--effect") == 0) {
        scenario->effect = value;
        return true;
    }
    if (strcmp(name, "--scene") == 0) {
        scenario->scene = value;
        return strlen(value) <= 48u;
    }
    if (strcmp(name, "--out") == 0) {
        scenario->out = value;
        return strlen(value) + 16u < PREVIEW_PATH_CAPACITY;
    }
    if (strcmp(name, "--needs") == 0)
        return parse_needs(value, scenario);
    for (size_t i = 0u; i < sizeof(options) / sizeof(options[0]); ++i) {
        if (strcmp(name, options[i].name) == 0) {
            long *field = (long *)(void *)((char *)scenario + options[i].offset);
            return parse_long(value, options[i].low, options[i].high, field);
        }
    }
    return false;
}

static bool parse_args(int argc, char **argv, Scenario *scenario)
{
    *scenario = (Scenario){.entry = 1, .minute = -1, .step_ms = 200, .frames = 1};
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        scenario->needs[i] = 700;
    if (argc % 2 != 1)
        return false;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!parse_option(argv[i], argv[i + 1], scenario)) {
            fprintf(stderr, "Invalid option or value: %s %s\n", argv[i], argv[i + 1]);
            return false;
        }
    }
    return scenario->out != NULL && (unsigned)scenario->entry <= jelli_collection_entry_count;
}

static const char *fail(const char *message)
{
    printf("{\"ok\":false,\"error\":\"%s\"}\n", message);
    return message;
}

/* The active pet becomes the scenario's collection entry in the requested form. */
static const char *apply_pet(const Scenario *s)
{
    const JelliEvolutionSet *set = jelli_collection_set((unsigned)s->entry);
    if (!set || (unsigned)s->form >= jelli_collection_form_count ||
        jelli_evolution_index(set, (unsigned)s->form) < 0)
        return fail("form is not in this pet's evolution set");
    int found = jelli_collection_find(&game, (unsigned)s->entry);
    if (found >= 0)
        game.active = (uint8_t)found;
    else
        game.pets[game.active] =
            jelli_collection_new_pet(game.pets[game.active].id, (unsigned)s->entry);
    JelliPet *pet = &game.pets[game.active];
    pet->form = (uint8_t)s->form;
    pet->reached_forms |= (uint8_t)((1u << set->forms[0]) | (1u << pet->form));
    for (unsigned i = 0u; i < JELLI_NEED_COUNT; ++i)
        pet->needs[i] = (uint16_t)s->needs[i];
    pet->potty = (uint16_t)s->potty;
    if ((unsigned)s->location >= jelli_location_count)
        return fail("unknown location");
    pet->location = (uint8_t)s->location;
    if (s->present_state) {
        if (!s->present || (unsigned)s->present > jelli_prize_count)
            return fail("unknown present");
        unsigned index = (unsigned)s->present - 1u;
        if (s->present_state == 1) {
            game.prizes.offered = (uint8_t)s->present;
            game.prizes.offered_pet = pet->id;
        } else {
            game.prizes.owned |= (uint16_t)(1u << index);
            game.prizes.discovered |= (uint16_t)(1u << index);
            game.prizes.origin_pet[index] = pet->id;
        }
    }
    if (s->minute >= 0) {
        game.clock_known = ui.clock_known = true;
        game.clock_minute = ui.clock_minute = (uint16_t)s->minute;
    }
    return jelli_game_valid(&game) ? NULL : fail("the engine rejects this pet setup");
}

static const char *apply_command(JelliCommandKind kind, uint32_t value, const char *refusal)
{
    JelliCommand command = {kind, game.pets[game.active].id, value};
    JelliResult result = jelli_game_command(&game, command);
    (void)jelli_pet_rewards_process(&ui.rewards, &game, false, false, 0u);
    if (result == JELLI_OK)
        return NULL;
    printf("{\"ok\":false,\"error\":\"%s: %s\"}\n", refusal, jelli_game_result_name(result));
    return refusal;
}

/* Small steps avoid the live catch-up limit and observe every completion event. */
static void advance_preview(uint64_t elapsed_ms)
{
    while (elapsed_ms) {
        uint64_t step = elapsed_ms > 100u ? 100u : elapsed_ms;
        jelli_game_advance(&game, step);
        (void)jelli_pet_rewards_process(&ui.rewards, &game, false, false, 0u);
        elapsed_ms -= step;
    }
}

/* Complete using the engine clock and command path. Two runs test the daily policy. */
static const char *complete_moment(const Scenario *s)
{
    if (!s->completion)
        return NULL;
    if (!s->moment)
        return fail("completion requires a moment");
    for (long run = 0; run < s->completion; ++run) {
        if (run && apply_command(JELLI_CMD_MOMENT, (uint32_t)(s->moment - 1), "repeat refused"))
            return "repeat";
        unsigned seconds = jelli_moments[s->moment - 1].duration_s;
        advance_preview((uint64_t)seconds * 1000u);
    }
    return NULL;
}

static const char *apply_state(const Scenario *s)
{
    if (s->moment) {
        if ((unsigned)s->moment > jelli_moment_count)
            return fail("unknown moment");
        if (apply_command(JELLI_CMD_MOMENT, (uint32_t)(s->moment - 1), "moment refused"))
            return "moment";
    }
    if (complete_moment(s))
        return "completion";
    if (s->asleep && apply_command(JELLI_CMD_REST, 0u, "rest refused"))
        return "rest";
    JelliPet *pet = &game.pets[game.active];
    if (s->behavior) {
        if ((unsigned)s->behavior > jelli_behavior_state_count)
            return fail("unknown behaviour state");
        pet->behavior = (uint8_t)s->behavior;
        /* Hold the state for its longest authored duration, as entering it could. */
        uint32_t ticks = jelli_behavior_states[s->behavior - 1].max_ticks;
        uint32_t seconds = (ticks + JELLI_BEHAVIOR_TICKS - 1u) / JELLI_BEHAVIOR_TICKS;
        pet->behavior_left =
            (uint16_t)(seconds ? (seconds > UINT16_MAX ? UINT16_MAX : seconds) : 1u);
    }
    if (s->mess)
        pet->behavior_flags |= JELLI_PET_FLAG_MESS;
    advance_preview((uint64_t)s->advance_s * 1000u);
    ui.page = (JelliPetPage)s->page;
    ui.menu_open = s->menu != 0;
    return jelli_game_valid(&game) ? NULL : fail("the engine rejects this scenario state");
}

static bool write_frame(const char *directory, unsigned index)
{
    char path[PREVIEW_PATH_CAPACITY];
    int size = snprintf(path, sizeof(path), "%s/frame-%03u.ppm", directory, index);
    if (size < 0 || (size_t)size >= sizeof(path))
        return false;
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    bool ok = fprintf(file, "P6\n%u %u\n255\n", (unsigned)JELLI_WIDTH, (unsigned)JELLI_HEIGHT) > 0;
    for (unsigned y = 0u; ok && y < JELLI_HEIGHT; ++y) {
        for (unsigned x = 0u; x < JELLI_WIDTH; ++x) {
            uint16_t c = pixels[y * JELLI_WIDTH + x];
            unsigned r = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
            row[x * 3u] = (uint8_t)(r << 3 | r >> 2);
            row[x * 3u + 1u] = (uint8_t)(g << 2 | g >> 4);
            row[x * 3u + 2u] = (uint8_t)(b << 3 | b >> 2);
        }
        ok = fwrite(row, 1u, sizeof(row), file) == sizeof(row);
    }
    return fclose(file) == 0 && ok;
}

static void advance_scene(const Scenario *s, int32_t *remainder, bool first)
{
    if (!s->scene)
        return;
    int32_t distance = first ? 0 : (int32_t)(s->travel_speed * s->step_ms * 256) + *remainder;
    *remainder = distance % 1000;
    distance /= 1000;
    /* Largest preview step is 320 source pixels; two bounded core updates suffice. */
    for (unsigned part = 0; part < 2; ++part) {
        int32_t chunk = distance < -JELLI_SCENE_DELTA_LIMIT  ? -JELLI_SCENE_DELTA_LIMIT
                        : distance > JELLI_SCENE_DELTA_LIMIT ? JELLI_SCENE_DELTA_LIMIT
                                                             : distance;
        jelli_scene_advance(&scene, (int16_t)(s->tilt_x * 256 / 100),
                            (int16_t)(s->tilt_y * 256 / 100), chunk);
        distance -= chunk;
    }
}

static bool render_frames(const Scenario *s, JelliSurface *surface)
{
    uint64_t start = (uint64_t)s->start_ms, step = (uint64_t)s->step_ms;
    int32_t remainder = 0;
    for (unsigned i = 0u; i < (unsigned)s->frames; ++i) {
        if (i && s->live)
            advance_preview(step);
        advance_scene(s, &remainder, i == 0);
        jelli_pet_render(surface, &game, &ui, start + (uint64_t)i * step, false);
        if (!ui.rendered || !write_frame(s->out, i))
            return false;
    }
    return true;
}

static const char *activity_name(JelliActivity activity)
{
    switch (activity) {
    case JELLI_IDLE:
        return "idle";
    case JELLI_EATING:
        return "eating";
    case JELLI_PLAYING:
        return "playing";
    case JELLI_CLEANING:
        return "cleaning";
    case JELLI_CARING:
        return "caring";
    case JELLI_GIVING:
        return "giving";
    case JELLI_EXERCISING:
        return "exercising";
    }
    return "unknown";
}

static void report(const Scenario *s)
{
    const JelliPet *pet = &game.pets[game.active];
    const JelliBehaviorState *state = jelli_behavior_current(pet);
    printf("{\"ok\":true,\"frames\":%ld,\"entry\":%u,\"form\":%u,\"form_name\":\"%s\","
           "\"behavior\":\"%s\",\"asleep\":%s,\"moment\":%u,\"activity\":\"%s\",\"mess\":%s,"
           "\"pose\":%u,\"page\":%u,\"menu\":%s,\"night\":%u,\"clock_minute\":%u,"
           "\"needs\":[%u,%u,%u,%u,%u],\"potty\":%u,"
           "\"location\":%u,\"location_name\":\"%s\",\"owned\":%u,\"offered\":%u,"
           "\"completed_moments\":%u,\"bond\":%u,\"hydration\":%u,"
           "\"rendered_location\":%u,\"visible_offered\":%u}\n",
           s->frames, (unsigned)pet->collection_entry, (unsigned)pet->form,
           jelli_collection_forms[pet->form].name, state ? state->name : "",
           pet->asleep ? "true" : "false", (unsigned)pet->moment, activity_name(pet->activity),
           (pet->behavior_flags & JELLI_PET_FLAG_MESS) ? "true" : "false",
           (unsigned)ui.last_view.pose, (unsigned)ui.page, ui.menu_open ? "true" : "false",
           (unsigned)ui.last_view.night, (unsigned)ui.last_view.clock_minute,
           (unsigned)pet->needs[0], (unsigned)pet->needs[1], (unsigned)pet->needs[2],
           (unsigned)pet->needs[3], (unsigned)pet->needs[4], (unsigned)pet->potty,
           (unsigned)pet->location, jelli_locations[pet->location].name,
           (unsigned)game.prizes.owned, (unsigned)game.prizes.offered,
           (unsigned)pet->completed_moments, (unsigned)pet->bond, (unsigned)pet->hydration,
           (unsigned)ui.last_view.location, (unsigned)ui.last_view.offered_prize);
}

int main(int argc, char **argv)
{
    Scenario scenario;
    if (!parse_args(argc, argv, &scenario)) {
        fprintf(stderr, "Usage: jelli_game_preview --out DIR [--entry N] [--form N] "
                        "[--page N] [--menu 0|1] [--behavior STATE+1] [--needs a,b,c,d,e] "
                        "[--potty 0..1000] [--mess 0|1] [--asleep 0|1] [--moment ID+1] "
                        "[--minute -1..1439] [--start_ms MS] [--step_ms MS] [--frames 1..48] "
                        "[--advance_s S] [--live 0|1]\n");
        return 2;
    }
    jelli_game_init(&game);
    game.events = &events;
    jelli_pet_ui_init(&ui);
    if (apply_pet(&scenario) || apply_state(&scenario))
        return 3;
    if (scenario.scene && !jelli_pet_scene_select(&ui, &scene, scenario.scene)) {
        fail("scene is unavailable");
        return 3;
    }
    if (scenario.effect && !jelli_pet_effect_start(&ui, scenario.effect, 0u)) {
        fail("effect is unavailable");
        return 3;
    }
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    if (!render_frames(&scenario, &surface)) {
        fail("could not render or write a frame");
        return 1;
    }
    report(&scenario);
    return 0;
}
