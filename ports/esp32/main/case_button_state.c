#include "case_button_state.h"
enum { DEBOUNCE_MS = 40, MAX_PRESS_MS = 1000 };
bool jelli_case_button_sample(JelliCaseButtonState *state, bool valid, bool pressed, uint64_t now)
{
    if (!valid) {
        state->initialized = false;
        return false;
    }
    if (!state->initialized) {
        *state = (JelliCaseButtonState){.initialized = true,
                                        .raw = pressed,
                                        .stable = pressed,
                                        .armed = !pressed,
                                        .changed_ms = now};
        return false;
    }
    if (pressed != state->raw) {
        state->raw = pressed;
        state->changed_ms = now;
    }
    if (pressed == state->stable || now - state->changed_ms < DEBOUNCE_MS)
        return false;
    state->stable = pressed;
    if (pressed) {
        state->pressed_ms = now;
        return false;
    }
    bool short_press = state->armed && now - state->pressed_ms <= MAX_PRESS_MS;
    state->armed = true;
    return short_press;
}
