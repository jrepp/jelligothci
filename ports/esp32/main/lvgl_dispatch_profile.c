#include "pipeline_profile.h"
#include "src/draw/lv_draw.h"

/* The pinned configuration draws synchronously in the LVGL task. This counter
 * excludes nested wrapped calls, not time spent in the original renderer.
 * Link wrapping does not intercept same-object dispatch calls inside finalize. */
_Static_assert(LV_USE_OS == LV_OS_NONE && LV_DRAW_SW_DRAW_UNIT_CNT == 1,
               "Review dispatch profiling before enabling asynchronous draw units");
static unsigned depth;

void __real_lv_draw_finalize_task_creation(lv_layer_t *layer, lv_draw_task_t *task);
void __wrap_lv_draw_finalize_task_creation(lv_layer_t *layer, lv_draw_task_t *task);
void __real_lv_draw_dispatch(void);
void __wrap_lv_draw_dispatch(void);
void __real_lv_draw_dispatch_wait_for_request(void);
void __wrap_lv_draw_dispatch_wait_for_request(void);

static void finish(JelliProfileStage stage, uint64_t start)
{
    --depth;
    if (!depth)
        jelli_profile_record(stage, start, 0u);
}

void __wrap_lv_draw_finalize_task_creation(lv_layer_t *layer, lv_draw_task_t *task)
{
    ++depth;
    uint64_t start = jelli_profile_now();
    __real_lv_draw_finalize_task_creation(layer, task);
    finish(JELLI_PROFILE_LVGL_TASKS, start);
}

void __wrap_lv_draw_dispatch(void)
{
    ++depth;
    uint64_t start = jelli_profile_now();
    __real_lv_draw_dispatch();
    finish(JELLI_PROFILE_LVGL_DISPATCH, start);
}

void __wrap_lv_draw_dispatch_wait_for_request(void)
{
    ++depth;
    uint64_t start = jelli_profile_now();
    __real_lv_draw_dispatch_wait_for_request();
    finish(JELLI_PROFILE_LVGL_DRAW_WAIT, start);
}

/* Render-object event time includes synchronous task/draw work. It is a
 * containing interval, not a fourth disjoint dispatch interval. */
static unsigned event_depth;
lv_result_t __real_lv_obj_send_event(lv_obj_t *obj, lv_event_code_t code, void *param);
lv_result_t __wrap_lv_obj_send_event(lv_obj_t *obj, lv_event_code_t code, void *param);
lv_result_t __wrap_lv_obj_send_event(lv_obj_t *obj, lv_event_code_t code, void *param)
{
    bool draw_event = code == LV_EVENT_COVER_CHECK ||
                      (code >= LV_EVENT_DRAW_MAIN_BEGIN && code <= LV_EVENT_DRAW_POST_END);
    if (!draw_event)
        return __real_lv_obj_send_event(obj, code, param);
    ++event_depth;
    uint64_t start = jelli_profile_now();
    lv_result_t result = __real_lv_obj_send_event(obj, code, param);
    --event_depth;
    if (!event_depth)
        jelli_profile_record(JELLI_PROFILE_LVGL_EVENTS, start, 0u);
    return result;
}

/* The engine and LVGL mutex serialize mode changes. Preserve original style,
 * and reject layers that could contribute pixels or contain future widgets. */
static bool overlay_changed[2], overlay_local[2];
static lv_style_value_t overlay_previous[2];
static bool empty_transparent(lv_obj_t *obj)
{
    return obj && lv_obj_get_child_count(obj) == 0u && lv_obj_get_event_count(obj) == 0u &&
           lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == LV_OPA_TRANSP &&
           lv_obj_get_style_bg_image_src(obj, LV_PART_MAIN) == NULL &&
           lv_obj_get_style_border_width(obj, LV_PART_MAIN) == 0 &&
           lv_obj_get_style_shadow_width(obj, LV_PART_MAIN) == 0 &&
           lv_obj_get_style_outline_width(obj, LV_PART_MAIN) == 0;
}

static bool restore_overlay(lv_obj_t *obj, unsigned index)
{
    if (overlay_local[index])
        lv_obj_set_local_style_prop(obj, LV_STYLE_OPA_LAYERED, overlay_previous[index],
                                    LV_PART_MAIN);
    else
        lv_obj_remove_local_style_prop(obj, LV_STYLE_OPA_LAYERED, LV_PART_MAIN);
    lv_style_value_t value;
    bool local = lv_obj_get_local_style_prop(obj, LV_STYLE_OPA_LAYERED, &value, LV_PART_MAIN) ==
                 LV_STYLE_RES_FOUND;
    return local == overlay_local[index] && (!local || value.num == overlay_previous[index].num);
}

bool jelli_profile_overlay_mode(bool skipped)
{
    lv_obj_t *layers[2] = {lv_layer_top(), lv_layer_sys()};
    if (skipped && (!empty_transparent(layers[0]) || !empty_transparent(layers[1])))
        return false;
    for (unsigned i = 0; i < 2u; ++i) {
        if (skipped && !overlay_changed[i]) {
            overlay_local[i] =
                lv_obj_get_local_style_prop(layers[i], LV_STYLE_OPA_LAYERED, &overlay_previous[i],
                                            LV_PART_MAIN) == LV_STYLE_RES_FOUND;
            /* LVGL refr_obj returns before drawing when layered opacity is zero.
             * Avoid HIDDEN: pinned root-layer unhide dereferences a null parent. */
            lv_obj_set_style_opa_layered(layers[i], LV_OPA_TRANSP, LV_PART_MAIN);
            overlay_changed[i] = true;
        } else if (!skipped && overlay_changed[i]) {
            if (!restore_overlay(layers[i], i))
                return false;
            overlay_changed[i] = false;
        }
    }
    return true;
}
