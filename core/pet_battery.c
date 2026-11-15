#include "pet_canvas.h"
#include <stdio.h>

void jelli_pet_draw_battery(Canvas *c, const JelliPetUi *ui)
{
    char value[18];
    int size;
    if (ui->battery_voltage_valid && ui->battery_gauge_valid)
        size = snprintf(value, sizeof(value), "BAT ~%u%%", ui->battery_percent);
    else if (ui->battery_voltage_valid)
        size = snprintf(value, sizeof(value), "BAT %u.%03uV", ui->battery_mv / 1000u,
                        ui->battery_mv % 1000u);
    else
        size = snprintf(value, sizeof(value), "%s",
                        ui->battery_present ? "BAT UNKNOWN" : "BAT UNAVAILABLE");
    if (ui->battery_reading_valid && (ui->battery_status[0] & 0x20u)) {
        bool charging = ui->battery_present && (ui->battery_status[1] & 0x60u) == 0x20u &&
                        (ui->battery_status[1] & 0x07u) < 4u;
        jelli_canvas_centered(c, charging ? "CHARGING" : "PLUGGED", 122, 2u, MINT);
    }
    if (size > 0 && (size_t)size < sizeof(value))
        jelli_canvas_centered(c, value, 78, 2u, PALE);
    if (ui->battery_gauge_valid) {
        jelli_canvas_rect(c, 169, 100, 128, 20, PALE);
        jelli_canvas_rect(c, 171, 102, 124, 16, BG);
        unsigned percent = ui->battery_percent <= 100u ? ui->battery_percent : 100u;
        jelli_canvas_rect(c, 171, 102, (int)(percent * 124u / 100u), 16, MINT);
    }
}
