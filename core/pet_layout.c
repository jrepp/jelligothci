#include "jelli/pet_ui.h"

static uint32_t action_icon(JelliPetUiAction action, bool asleep)
{
    static const uint32_t icons[] = {6001u, 6002u, 6003u, 6004u, 6008u, 6006u, 2001u, 2003u,
                                     2004u, 2011u, 2008u, 2009u, 6011u, 6004u, 2005u, 2012u,
                                     6007u, 6009u, 6010u, 6011u, 6012u, 6007u, 8002u, 8001u,
                                     8002u, 8003u, 8004u, 8005u, 6015u, 6014u, 0u,    0u};
    if (asleep && action == JELLI_UI_ACTION_REST_WAKE)
        return 2006u;
    return (unsigned)action < sizeof(icons) / sizeof(icons[0]) ? icons[action] : 6005u;
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

static unsigned moment_hour(unsigned hour)
{
    if (hour >= 5u && hour < 11u)
        return 0u;
    if (hour >= 11u && hour < 15u)
        return 1u;
    if (hour >= 15u && hour < 19u)
        return 2u;
    return 3u;
}

unsigned jelli_pet_moment(const JelliPet *pet)
{
    uint64_t phase = (pet->ticks % JELLI_DAY_TICKS + pet->phase_offset) % JELLI_DAY_TICKS;
    return moment_hour((unsigned)(phase / 36000u));
}

unsigned jelli_pet_suggested_moment(const JelliPet *pet, const JelliPetUi *ui)
{
    return moment_hour(jelli_pet_clock_minute(ui, pet) / 60u);
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
