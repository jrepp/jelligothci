/* Reference model of the engine's screen gate for property tests, written from memo-023
 * and the pet_engine.h contract rather than from core/pet_screen.c. */
#ifndef JELLI_TEST_SCREEN_GATE_H
#define JELLI_TEST_SCREEN_GATE_H
#include "jelli/engine.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t anchor, tap_ms, wake_ms;
    int tap_x, tap_y;
    bool dark, pending, guard;
    bool reported_dark; /* Last state the host change query reported. */
    bool redraw;        /* Woke since the last frame: the next frame must repaint fully. */
} ScreenGate;

void screen_gate_init(ScreenGate *gate, uint64_t now);
/* One polled input at `now`; true when the UI must receive it. */
bool screen_gate_admits(ScreenGate *gate, JelliInput input, uint64_t now);
/* The idle rule after a frame's input; true when it darkens the screen. */
bool screen_gate_idle(ScreenGate *gate, uint64_t now);
/* Forced (debug) transitions; sleeping a dark screen changes nothing. */
void screen_gate_wake(ScreenGate *gate, uint64_t now);
void screen_gate_sleep(ScreenGate *gate);
/* Milliseconds until the idle rule fires; zero while dark. */
uint64_t screen_gate_remaining(const ScreenGate *gate, uint64_t now);
#endif
