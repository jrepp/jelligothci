#include "debug_internal.h"
#include <inttypes.h>
#include <stdio.h>

static size_t buttons(char *out, size_t capacity, const JelliPetUi *ui)
{
    if (ui->last_view.page >= JELLI_UI_PAGE_COUNT)
        return 0;
    size_t used = 0;
    for (unsigned i = 0; i <= 6u; ++i) {
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
    return view->stat_index
               ? jelli_pet_stat_score(view->needs[(view->stat_index - 1u) % JELLI_NEED_COUNT])
               : view->mood;
}

void jelli_debug_state(JelliDebug *debug, const JelliPetEngine *engine, uint32_t id)
{
    const JelliPetRenderKey *v = &engine->ui.last_view;
    static const char *const pages[] = {"home",     "care",    "more",   "collection",
                                        "settings", "moments", "health", "brush",
                                        "medicine", "shot",    "wash",   "stretch"};
    const char *page = v->page < JELLI_UI_PAGE_COUNT ? pages[v->page] : "unknown";
    int size = snprintf(
        debug->reply, sizeof(debug->reply),
        "\n@J1 %" PRIu32 " {\"ok\":true,\"version\":1,\"width\":466,\"height\":466,"
        "\"capture\":%" PRIu32 ",\"rendered\":%s,\"transitioning\":%s,\"ticks\":%" PRIu64 ","
        "\"visual\":{\"page\":\"%s\",\"pet_id\":%" PRIu32 ",\"form\":%u,\"location\":%u,"
        "\"clicker_hits\":%u,\"clicker_goal\":%u,\"clicker_stage\":%u,\"clicker_done\":%s,"
        "\"health\":%u,\"activity\":%u,\"asleep\":%s,\"animation_phase\":"
        "%" PRIu32 ","
        "\"clock_edit\":%s,\"timezone_minutes\":%d,\"clock_known\":%s,\"clock_minute\":%u,\"menu_"
        "open\":%s,\"stat_index\":%u,\"stat_score\":%"
        "u,\"tile_phase\":%u,\"paused\":%s,\"resuming\":%s,\"time_"
        "unavailable\":%s,\"save_status\":%u,"
        "\"result\":\"%s\",\"day\":%" PRIu64 ",\"minute\":%" PRIu32 ","
        "\"needs\":[%u,%u,%u,%u,%u],\"bond\":%u,\"food\":%u,\"gifts\":%u,"
        "\"reward_pending\":%s,\"reward_claimed\":%s,\"bedtime\":%u,"
        "\"active_slot\":%u,\"pet_count\":%u,\"stored_id\":%" PRIu32
        ",\"stored_form\":%u,\"stored_asleep\":%s,\"mood\":%u,\"reaction\":%u,\"night\":%u},"
        "\"buttons\":[",
        id, debug->captured ? debug->capture_id : 0u, engine->ui.rendered ? "true" : "false",
        v->ring_moving ? "true" : "false", engine->game.ticks, page, v->active_id,
        (unsigned)v->form, (unsigned)v->location, (unsigned)v->clicker_hits,
        (unsigned)v->clicker_goal, (unsigned)v->clicker_stage, v->clicker_done ? "true" : "false",
        (unsigned)v->health, (unsigned)v->activity, v->asleep ? "true" : "false", v->phase,
        v->clock_edit ? "true" : "false", (int)v->timezone_minutes,
        v->clock_known ? "true" : "false", (unsigned)v->clock_minute,
        v->menu_open ? "true" : "false", (unsigned)v->stat_index, stat_score(v),
        (unsigned)v->tile_phase, v->paused ? "true" : "false", v->resuming ? "true" : "false",
        v->time_unavailable ? "true" : "false", (unsigned)v->save_status,
        jelli_game_result_name(v->result), v->day, v->minute, (unsigned)v->needs[0],
        (unsigned)v->needs[1], (unsigned)v->needs[2], (unsigned)v->needs[3], (unsigned)v->needs[4],
        (unsigned)v->bond, (unsigned)v->food, (unsigned)v->gifts,
        v->reward_pending ? "true" : "false", v->reward_claimed ? "true" : "false",
        (unsigned)v->bedtime, (unsigned)v->active, (unsigned)v->count, v->stored_id,
        (unsigned)v->stored_form, v->stored_asleep ? "true" : "false", (unsigned)v->mood,
        (unsigned)v->reaction, (unsigned)v->night);
    if (size < 0 || (size_t)size >= sizeof(debug->reply))
        return;
    size_t used = (size_t)size;
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
