#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>

static const char *truth(bool value) { return value ? "true" : "false"; }

static size_t buttons(char *out, size_t capacity, const JelliPetUi *ui)
{
    if (ui->last_view.page >= JELLI_UI_PAGE_COUNT)
        return 0;
    size_t used = 0;
    for (unsigned i = 0; i <= JELLI_PRIZE_COUNT; ++i) {
        if (i && ui->last_view.ring_moving)
            continue;
        JelliPetUiButton button;
        if (!jelli_pet_ui_control(ui, i, ui->last_view.asleep, &button))
            continue;
        int size =
            snprintf(out + used, capacity - used,
                     "%s{\"id\":%u,\"label\":\"%s\",\"x\":%u,\"y\":%u,\"icon\":%u,\"enabled\":%s}",
                     used ? "," : "", i, button.label, button.bounds.x + button.bounds.width / 2u,
                     button.bounds.y + button.bounds.height / 2u, (unsigned)button.icon,
                     (ui->last_view.unavailable & (1u << i)) ? "false" : "true");
        if (size < 0 || (size_t)size >= capacity - used)
            return 0;
        used += (size_t)size;
    }
    return used;
}

static unsigned stat_score(const JelliPetRenderKey *view)
{
    return view->stat_index == 8u ? view->stat_value / 10u : jelli_pet_stat_score(view->stat_value);
}

static size_t collection(char *out, size_t capacity, const JelliPetRenderKey *v)
{
    unsigned owned = 0u;
    for (unsigned i = 0u; i < JELLI_PRIZE_COUNT; ++i)
        if (v->prize_owned & (1u << i))
            ++owned;
    const uint32_t *origins = v->prize_origins;
    int size = snprintf(
        out, capacity,
        ",\"collection\":{\"owned_count\":%u,\"owned_mask\":%u,\"discovered_mask\":%u,"
        "\"offered\":%u,\"held\":%u,\"highlighted\":%u,\"origin_pet\":["
        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]}},\"buttons\":[",
        owned, (unsigned)v->prize_owned, (unsigned)v->prize_discovered, (unsigned)v->offered_prize,
        (unsigned)v->latched_prize, (unsigned)v->highlighted_prize, origins[0], origins[1],
        origins[2], origins[3], origins[4], origins[5], origins[6], origins[7], origins[8]);
    return size < 0 || (size_t)size >= capacity ? 0u : (size_t)size;
}

void jelli_debug_state(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id)
{
    const JelliPetRenderKey *v = &engine->ui.last_view;
    static const char *const pages[JELLI_UI_PAGE_COUNT] = {[JELLI_UI_HOME] = "home",
                                                           [JELLI_UI_CARE] = "care",
                                                           [JELLI_UI_MORE] = "more",
                                                           [JELLI_UI_COLLECTION] = "collection",
                                                           [JELLI_UI_SETTINGS] = "settings",
                                                           [JELLI_UI_MOMENTS] = "moments",
                                                           [JELLI_UI_HEALTH] = "health",
                                                           [JELLI_UI_BRUSH] = "brush",
                                                           [JELLI_UI_MEDICINE] = "medicine",
                                                           [JELLI_UI_SHOT] = "shot",
                                                           [JELLI_UI_WASH] = "wash",
                                                           [JELLI_UI_STRETCH] = "stretch",
                                                           [JELLI_UI_POTTY] = "potty",
                                                           [JELLI_UI_PETS] = "pets",
                                                           [JELLI_UI_PET_DETAIL] = "pet_detail",
                                                           [JELLI_UI_EVOLUTIONS] = "evolutions",
                                                           [JELLI_UI_PRESENT_ACTION] =
                                                               "present_action",
                                                           [JELLI_UI_FOOD] = "food"};
    const char *page = v->page < JELLI_UI_PAGE_COUNT ? pages[v->page] : "unknown";
    int size = snprintf(
        debug->reply, sizeof(debug->reply),
        "\n@J1 %" PRIu32 " {\"ok\":true,\"version\":1,\"width\":466,\"height\":466,"
        "\"capture\":%" PRIu32 ",\"rendered\":%s,\"transitioning\":%s,\"ticks\":%" PRIu64 ","
        "\"visual\":{\"page\":\"%s\",\"page_id\":%u,\"pet_id\":%" PRIu32
        ",\"form\":%u,\"location\":%u,"
        "\"clicker_hits\":%u,\"clicker_goal\":%u,\"clicker_stage\":%u,\"clicker_done\":%s,"
        "\"health\":%u,\"activity\":%u,\"asleep\":%s,\"animation_phase\":"
        "%" PRIu32 ","
        "\"clock_edit\":%s,\"timezone_minutes\":%d,\"clock_known\":%s,\"clock_minute\":%u,\"menu_"
        "open\":%s,\"stat_index\":%u,\"stat_score\":%"
        "u,\"reward_active\":%s,\"reward_index\":%u,\"tile_phase\":%u,\"paused\":%s,\"resuming\":%"
        "s,\"time_"
        "unavailable\":%s,\"save_status\":%u,"
        "\"result\":\"%s\",\"day\":%" PRIu64 ",\"minute\":%" PRIu32 ","
        "\"needs\":[%u,%u,%u,%u,%u],\"bond\":%u,\"hydration\":%u,\"volume\":%u,\"food\":%u,"
        "\"gifts\":%u,"
        "\"reward_pending\":%s,\"reward_claimed\":%s,\"bedtime\":%u,"
        "\"active_slot\":%u,\"pet_count\":%u,\"stored_id\":%" PRIu32
        ",\"stored_form\":%u,\"stored_asleep\":%s,\"mood\":%u,\"reaction\":%u,\"night\":%u",
        id, debug->captured ? debug->capture_id : 0u, truth(engine->ui.rendered),
        truth(v->ring_moving), engine->game.ticks, page, (unsigned)v->page, v->active_id,
        (unsigned)v->form, (unsigned)v->location, (unsigned)v->clicker_hits,
        (unsigned)v->clicker_goal, (unsigned)v->clicker_stage, truth(v->clicker_done),
        (unsigned)v->health, (unsigned)v->activity, truth(v->asleep), v->phase,
        truth(v->clock_edit), (int)v->timezone_minutes, truth(v->clock_known),
        (unsigned)v->clock_minute, truth(v->menu_open), (unsigned)v->stat_index, stat_score(v),
        truth(v->reward_active), (unsigned)v->reward_index, (unsigned)v->tile_phase,
        truth(v->paused), truth(v->resuming), truth(v->time_unavailable), (unsigned)v->save_status,
        jelli_game_result_name(v->result), v->day, v->minute, (unsigned)v->needs[0],
        (unsigned)v->needs[1], (unsigned)v->needs[2], (unsigned)v->needs[3], (unsigned)v->needs[4],
        (unsigned)v->bond, (unsigned)v->hydration, (unsigned)v->volume, (unsigned)v->food,
        (unsigned)v->gifts, truth(v->reward_pending), truth(v->reward_claimed),
        (unsigned)v->bedtime, (unsigned)v->active, (unsigned)v->count, v->stored_id,
        (unsigned)v->stored_form, truth(v->stored_asleep), (unsigned)v->mood, (unsigned)v->reaction,
        (unsigned)v->night);
    if (size < 0 || (size_t)size >= sizeof(debug->reply))
        return;
    size_t used = (size_t)size;
    size_t summary = collection(debug->reply + used, sizeof(debug->reply) - used, v);
    if (!summary)
        return;
    used += summary;
    size_t extra = buttons(debug->reply + used, sizeof(debug->reply) - used, &engine->ui);
    if (!extra || used + extra + 4u >= sizeof(debug->reply))
        return;
    used += extra;
    debug->reply[used++] = ']';
    debug->reply[used++] = '}';
    debug->reply[used++] = '\n';
    debug->reply[used] = '\0';
    debug->reply_size = used;
}
