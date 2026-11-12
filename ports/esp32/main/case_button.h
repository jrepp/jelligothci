#ifndef JELLI_CASE_BUTTON_H
#define JELLI_CASE_BUTTON_H
#include "display_output.h"
#include "case_button_state.h"
#include "touch_input.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t io;
    JelliDisplayOutput *display;
    JelliTouchGuard *touch;
    JelliCaseButtonState state;
    unsigned restore_brightness;
    bool blank;
} JelliCaseButton;
void jelli_case_button_init(JelliCaseButton *button, JelliDisplayOutput *display,
                            JelliTouchGuard *touch);
void jelli_case_button_poll(JelliCaseButton *button, uint64_t now);
#endif
