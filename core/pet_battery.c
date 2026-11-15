#include "pet_canvas.h"
#include <stdio.h>

void jelli_pet_draw_battery(Canvas *c, const JelliPetUi *ui)
{
    char value[18];
    int size;
    if (ui->battery_voltage_valid && ui->battery_gauge_valid)
        size = snprintf(value, sizeof(value), "BAT %u.%03uV ~%u%%", ui->battery_mv / 1000u,
                        ui->battery_mv % 1000u, ui->battery_percent);
    else if (ui->battery_voltage_valid)
        size = snprintf(value, sizeof(value), "BAT %u.%03uV", ui->battery_mv / 1000u,
                        ui->battery_mv % 1000u);
    else
        size = snprintf(value, sizeof(value), "%s",
                        ui->battery_present ? "BAT UNKNOWN" : "BAT UNAVAILABLE");
    if (size > 0 && (size_t)size < sizeof(value))
        jelli_canvas_centered(c, value, 86, 1u, PALE);
    if (ui->battery_gauge_valid) {
        jelli_canvas_rect(c, 181, 102, 104, 8, PALE);
        jelli_canvas_rect(c, 183, 104, 100, 4, BG);
        jelli_canvas_rect(c, 183, 104, ui->battery_percent <= 100u ? ui->battery_percent : 100, 4,
                          MINT);
    }
}
