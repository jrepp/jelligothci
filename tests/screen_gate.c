#include "screen_gate.h"
#include "jelli/pet_screen.h"

/* Floating-point distance on purpose: it shares no arithmetic with the core. */
static bool near(int ax, int ay, int bx, int by)
{
    double dx = (double)ax - bx, dy = (double)ay - by;
    return dx * dx + dy * dy <= (double)JELLI_SCREEN_DOUBLE_TAP_PX * JELLI_SCREEN_DOUBLE_TAP_PX;
}

void screen_gate_init(ScreenGate *gate, uint64_t now) { *gate = (ScreenGate){.anchor = now}; }

void screen_gate_wake(ScreenGate *gate, uint64_t now)
{
    gate->redraw |= gate->dark;
    gate->dark = gate->pending = gate->guard = false;
    gate->anchor = now;
}

void screen_gate_sleep(ScreenGate *gate)
{
    if (gate->dark)
        return; /* A candidate first tap survives. */
    gate->dark = true;
    gate->pending = gate->guard = false;
}

bool screen_gate_admits(ScreenGate *gate, JelliInput input, uint64_t now)
{
    if (input.kind == JELLI_QUIT)
        return true;
    if (!gate->dark) {
        gate->anchor = now;
        if (input.kind != JELLI_TAP || !gate->guard)
            return true;
        if (now >= gate->wake_ms && now - gate->wake_ms <= JELLI_SCREEN_DOUBLE_TAP_MS)
            return false; /* The extra tap of a triple tap. */
        gate->guard = false;
        return true;
    }
    if (input.kind == JELLI_SWIPE)
        gate->pending = false;
    if (input.kind != JELLI_TAP)
        return false;
    if (gate->pending && now >= gate->tap_ms && now - gate->tap_ms <= JELLI_SCREEN_DOUBLE_TAP_MS &&
        near(gate->tap_x, gate->tap_y, input.x, input.y)) {
        screen_gate_wake(gate, now);
        gate->guard = true;
        gate->wake_ms = now;
        return false;
    }
    gate->pending = true; /* The window restarts from the latest tap. */
    gate->tap_ms = now;
    gate->tap_x = input.x;
    gate->tap_y = input.y;
    return false;
}

bool screen_gate_idle(ScreenGate *gate, uint64_t now)
{
    if (gate->dark)
        return false;
    if (now < gate->anchor) {
        gate->anchor = now;
        return false;
    }
    if (now - gate->anchor < JELLI_SCREEN_SLEEP_MS)
        return false;
    screen_gate_sleep(gate);
    return true;
}

uint64_t screen_gate_remaining(const ScreenGate *gate, uint64_t now)
{
    if (gate->dark)
        return 0u;
    if (now < gate->anchor)
        return JELLI_SCREEN_SLEEP_MS;
    uint64_t idle = now - gate->anchor;
    return idle >= JELLI_SCREEN_SLEEP_MS ? 0u : JELLI_SCREEN_SLEEP_MS - idle;
}
