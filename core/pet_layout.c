#include "jelli_asset_ids.h"
#include "jelli/pet_ui.h"
#include "pet_canvas.h"
#include "jelli/activities.h"

#define BUTTON_ICON_PX 96u /* Ring buttons are 96 px discs. */

static uint32_t action_icon(JelliPetUiAction action, bool asleep)
{
    /* Moment and health-routine icons come from their own tables. */
    static const uint32_t icons[JELLI_UI_ACTION_COUNT] = {
        [JELLI_UI_ACTION_FEED] = JELLI_ASSET_MENUS_FOOD,
        [JELLI_UI_ACTION_CARE] = JELLI_ASSET_MENUS_CARE,
        [JELLI_UI_ACTION_REST_WAKE] = JELLI_ASSET_MENUS_REST,
        [JELLI_UI_ACTION_COLLECTION] = JELLI_ASSET_MENUS_COLLECTION,
        [JELLI_UI_ACTION_MORE] = JELLI_ASSET_MENUS_GIFTS,
        [JELLI_UI_ACTION_SETTINGS] = JELLI_ASSET_MENUS_SETTINGS,
        [JELLI_UI_ACTION_BASIC_CARE] = JELLI_ASSET_ICONS_BASIC_CARE,
        [JELLI_UI_ACTION_PLAY] = JELLI_ASSET_ICONS_PLAY,
        [JELLI_UI_ACTION_CLEAN_WAKE] = JELLI_ASSET_ICONS_CLEAN,
        [JELLI_UI_ACTION_HOME] = JELLI_ASSET_ICONS_BACK,
        [JELLI_UI_ACTION_GIFT] = JELLI_ASSET_ICONS_GIFT,
        [JELLI_UI_ACTION_CLAIM] = JELLI_ASSET_ICONS_REWARD,
        [JELLI_UI_ACTION_TRAVEL] = JELLI_ASSET_MENUS_OUTING,
        [JELLI_UI_ACTION_SWITCH_PET] = JELLI_ASSET_MENUS_COLLECTION,
        [JELLI_UI_ACTION_BEDTIME] = JELLI_ASSET_ICONS_REST,
        [JELLI_UI_ACTION_SAVE] = JELLI_ASSET_ICONS_CONFIRM,
        [JELLI_UI_ACTION_MOMENTS] = JELLI_ASSET_MENUS_MOMENTS,
        [JELLI_UI_ACTION_SUGGEST] = JELLI_ASSET_MENUS_MOMENTS,
        [JELLI_UI_ACTION_HEALTH] = JELLI_ASSET_HEALTH_MEDICINE,
        [JELLI_UI_ACTION_WATER] = JELLI_ASSET_MENUS_WATER,
        [JELLI_UI_ACTION_EXERCISE] = JELLI_ASSET_MENUS_EXERCISE,
        [JELLI_UI_ACTION_VOLUME_DOWN] = 0u,
        [JELLI_UI_ACTION_VOLUME_UP] = 0u,
    };
    unsigned moment = 0u, activity = 0u;
    if (asleep && action == JELLI_UI_ACTION_REST_WAKE)
        return JELLI_ASSET_ICONS_WAKE;
    if (jelli_pet_moment_for_action(action, &moment))
        return jelli_moments[moment].icon;
    if (jelli_pet_routine_for_action(action, NULL, &activity))
        return jelli_pet_health_icon(activity);
    return (unsigned)action < JELLI_UI_ACTION_COUNT ? icons[action]
                                                    : JELLI_ASSET_MENUS_MORE; /* 0: label only. */
}

bool jelli_pet_ui_button(JelliPetPage page, unsigned slot, bool asleep, bool menu_open,
                         JelliPetUiButton *button)
{
    static const unsigned centers[6][2] = {{110u, 111u}, {356u, 111u}, {405u, 233u},
                                           {356u, 355u}, {110u, 355u}, {61u, 233u}};
    if (!button || (unsigned)page >= JELLI_UI_PAGE_COUNT || slot > 6u ||
        (slot && (!menu_open || page >= JELLI_UI_BRUSH)))
        return false;
    if (!slot) {
        *button = (JelliPetUiButton){
            .bounds =
                menu_open ? (JelliRect){185u, 414u, 96u, 52u} : (JelliRect){161u, 388u, 144u, 64u},
            .label = !menu_open ? "MENU" : (page == JELLI_UI_HOME ? "CLOSE" : "BACK"),
            .icon = !menu_open ? JELLI_ASSET_MENUS_MORE
                               : (page == JELLI_UI_HOME ? JELLI_ASSET_MENUS_CLOSE
                                                        : JELLI_ASSET_ICONS_BACK),
            .scale = menu_open && page != JELLI_UI_HOME ? 2u : 1u};
        return true;
    }
    JelliPetUiItem item = jelli_pet_ui_item(page, slot - 1u, asleep);
    if (!item.label[0])
        return false;
    uint32_t icon = action_icon(item.action, asleep);
    unsigned position = page == JELLI_UI_SETTINGS && slot == 6u ? 3u : slot - 1u;
    *button = (JelliPetUiButton){
        .bounds = {centers[position][0] - 48u, centers[position][1] - 48u, 96u, 96u},
        .label = item.label,
        .icon = icon,
        .scale = jelli_canvas_fit_scale(icon, BUTTON_ICON_PX),
        .circular = true};
    return true;
}

unsigned jelli_pet_moment(const JelliPet *pet)
{
    uint64_t phase = (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) % JELLI_DAY_TICKS;
    return jelli_moment_suggested((unsigned)(phase / 36000u));
}

unsigned jelli_pet_suggested_moment(const JelliPet *pet, const JelliPetUi *ui)
{
    return jelli_moment_suggested(jelli_pet_clock_minute(ui, pet) / 60u);
}

unsigned jelli_pet_stat_score(uint16_t value)
{
    unsigned score = ((unsigned)value + 9u) / 10u;
    if (!score)
        return 1u;
    return score > 100u ? 100u : score;
}

bool jelli_pet_ring_button(const JelliPetUi *ui, unsigned slot, bool asleep,
                           JelliPetUiButton *button)
{
    const JelliPetRenderKey *v = &ui->last_view;
    if (v->ring_page == JELLI_UI_SETTINGS && v->ring_clock_edit)
        return jelli_pet_clock_button(true, slot, button);
    return jelli_pet_ui_button((JelliPetPage)v->ring_page, slot, asleep, true, button);
}

bool jelli_pet_ui_starts(unsigned page, unsigned slot)
{
    return page == JELLI_UI_MOMENTS ||
           (page == JELLI_UI_CARE && (slot == 1u || slot == 3u || slot == 4u));
}
