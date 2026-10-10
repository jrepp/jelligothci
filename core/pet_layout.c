#include "jelli/pet_ui.h"
#include "jelli/activities.h"

static uint32_t action_icon(JelliPetUiAction action, bool asleep)
{
    /* Moment and health-routine icons come from their own tables. */
    static const uint32_t icons[JELLI_UI_ACTION_COUNT] = {
        [JELLI_UI_ACTION_FEED] = 6001u,       [JELLI_UI_ACTION_CARE] = 6002u,
        [JELLI_UI_ACTION_REST_WAKE] = 6003u,  [JELLI_UI_ACTION_COLLECTION] = 6004u,
        [JELLI_UI_ACTION_MORE] = 6008u,       [JELLI_UI_ACTION_SETTINGS] = 6006u,
        [JELLI_UI_ACTION_BASIC_CARE] = 2001u, [JELLI_UI_ACTION_PLAY] = 2003u,
        [JELLI_UI_ACTION_CLEAN_WAKE] = 2004u, [JELLI_UI_ACTION_HOME] = 2011u,
        [JELLI_UI_ACTION_GIFT] = 2008u,       [JELLI_UI_ACTION_CLAIM] = 2009u,
        [JELLI_UI_ACTION_TRAVEL] = 6011u,     [JELLI_UI_ACTION_SWITCH_PET] = 6004u,
        [JELLI_UI_ACTION_BEDTIME] = 2005u,    [JELLI_UI_ACTION_SAVE] = 2012u,
        [JELLI_UI_ACTION_MOMENTS] = 6007u,    [JELLI_UI_ACTION_SUGGEST] = 6007u,
        [JELLI_UI_ACTION_HEALTH] = 8002u,     [JELLI_UI_ACTION_WATER] = 6015u,
        [JELLI_UI_ACTION_EXERCISE] = 6014u,   [JELLI_UI_ACTION_VOLUME_DOWN] = 0u,
        [JELLI_UI_ACTION_VOLUME_UP] = 0u,
    };
    unsigned moment = 0u, activity = 0u;
    if (asleep && action == JELLI_UI_ACTION_REST_WAKE)
        return 2006u;
    if (jelli_pet_moment_for_action(action, &moment))
        return jelli_moments[moment].icon;
    if (jelli_pet_routine_for_action(action, NULL, &activity))
        return jelli_pet_health_icon(activity);
    return (unsigned)action < JELLI_UI_ACTION_COUNT ? icons[action] : 6005u; /* 0: label only. */
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
            .icon = !menu_open ? 6005u : (page == JELLI_UI_HOME ? 6013u : 2011u),
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
        .scale = icon >= 6000u ? 3u : 6u,
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
