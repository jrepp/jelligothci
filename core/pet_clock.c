#include "jelli/pet_ui.h"

uint16_t jelli_pet_clock_minute(const JelliPetUi *ui, const JelliPet *pet)
{
    unsigned base =
        ui->clock_known
            ? ui->clock_minute
            : (unsigned)((pet->ticks % JELLI_DAY_TICKS + pet->phase_offset % JELLI_DAY_TICKS) %
                         JELLI_DAY_TICKS / 600u);
    int minute = (int)base + ui->timezone_minutes + ui->clock_adjust;
    return (uint16_t)((minute % 1440 + 1440) % 1440);
}

bool jelli_pet_clock_button(bool editing, unsigned slot, JelliPetUiButton *button)
{
    static const char *const labels[] = {"TZ -", "TZ +", "HR -", "HR +", "MIN -", "MIN +"};
    static const unsigned centers[6][2] = {{110, 111}, {356, 111}, {61, 233},
                                           {405, 233}, {110, 355}, {356, 355}};
    if (!editing) {
        if (slot != 4u)
            return false;
        *button = (JelliPetUiButton){.bounds = {205, 239, 56, 56},
                                     .label = "CLOCK",
                                     .icon = 6006u,
                                     .scale = 1u,
                                     .circular = true};
        return true;
    }
    if (!slot || slot > 6u)
        return false;
    *button = (JelliPetUiButton){
        .bounds = {centers[slot - 1u][0] - 48u, centers[slot - 1u][1] - 48u, 96, 96},
        .label = labels[slot - 1u],
        .circular = true};
    return true;
}

void jelli_pet_clock_action(JelliPetUi *ui, unsigned slot)
{
    if (!ui->clock_edit) {
        ui->clock_edit = true;
    } else if (slot <= 2u) {
        int zone = ui->timezone_minutes + (slot == 1u ? -30 : 30);
        if (zone >= -720 && zone <= 840)
            ui->timezone_minutes = (int16_t)zone;
    } else {
        int delta = slot <= 4u ? 60 : 1;
        int adjusted = ui->clock_adjust + (slot % 2u ? -delta : delta);
        ui->clock_adjust = (int16_t)((adjusted % 1440 + 1440) % 1440);
    }
    ui->result = JELLI_OK;
}
