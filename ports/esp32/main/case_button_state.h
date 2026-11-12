#ifndef JELLI_CASE_BUTTON_STATE_H
#define JELLI_CASE_BUTTON_STATE_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint64_t changed_ms, pressed_ms;
    bool raw, stable, initialized, armed;
} JelliCaseButtonState;
/* A failed sample disarms until a valid release; true means one short press. */
bool jelli_case_button_sample(JelliCaseButtonState *state, bool valid, bool pressed, uint64_t now);
#endif
