#include "jelli/pet_screen.h"
#include <stddef.h>

void jelli_pet_screen_init(JelliPetScreen *screen, uint64_t now_ms)
{
    if (screen)
        *screen = (JelliPetScreen){.activity_ms = now_ms};
}

void jelli_pet_screen_activity(JelliPetScreen *screen, uint64_t now_ms)
{
    if (screen)
        screen->activity_ms = now_ms;
}

void jelli_pet_screen_set(JelliPetScreen *screen, bool asleep, uint64_t now_ms)
{
    if (!screen)
        return;
    screen->asleep = asleep;
    screen->tap_pending = false;
    screen->wake_guard = false;
    if (!asleep)
        screen->activity_ms = now_ms;
}

bool jelli_pet_screen_idle(JelliPetScreen *screen, uint64_t now_ms)
{
    if (!screen || screen->asleep)
        return false;
    if (now_ms < screen->activity_ms) {
        /* A regressing host clock restarts the timer rather than underflowing. */
        screen->activity_ms = now_ms;
        return false;
    }
    if (now_ms - screen->activity_ms < JELLI_SCREEN_SLEEP_MS)
        return false;
    jelli_pet_screen_set(screen, true, now_ms);
    return true;
}

uint64_t jelli_pet_screen_remaining(const JelliPetScreen *screen, uint64_t now_ms)
{
    if (!screen || screen->asleep)
        return 0u;
    if (now_ms < screen->activity_ms)
        return JELLI_SCREEN_SLEEP_MS; /* jelli_pet_screen_idle will re-anchor here. */
    uint64_t idle = now_ms - screen->activity_ms;
    return idle >= JELLI_SCREEN_SLEEP_MS ? 0u : JELLI_SCREEN_SLEEP_MS - idle;
}

/* Bounds each axis first so the squared distance cannot overflow for any int input. */
static bool near(int ax, int ay, int bx, int by)
{
    const int64_t limit = JELLI_SCREEN_DOUBLE_TAP_PX;
    int64_t dx = (int64_t)ax - (int64_t)bx, dy = (int64_t)ay - (int64_t)by;
    if (dx < -limit || dx > limit || dy < -limit || dy > limit)
        return false;
    return dx * dx + dy * dy <= limit * limit;
}

bool jelli_pet_screen_wake_tap(JelliPetScreen *screen, int x, int y, uint64_t now_ms)
{
    if (!screen || !screen->asleep)
        return false;
    bool paired = screen->tap_pending && now_ms >= screen->tap_ms &&
                  now_ms - screen->tap_ms <= JELLI_SCREEN_DOUBLE_TAP_MS &&
                  near(screen->tap_x, screen->tap_y, x, y);
    if (!paired) {
        /* The window restarts from the latest tap. */
        screen->tap_pending = true;
        screen->tap_ms = now_ms;
        screen->tap_x = x;
        screen->tap_y = y;
        return false;
    }
    jelli_pet_screen_set(screen, false, now_ms);
    screen->wake_guard = true;
    screen->wake_ms = now_ms;
    return true;
}

void jelli_pet_screen_cancel_tap(JelliPetScreen *screen)
{
    if (screen)
        screen->tap_pending = false;
}

bool jelli_pet_screen_guarded(JelliPetScreen *screen, uint64_t now_ms)
{
    if (!screen || !screen->wake_guard)
        return false;
    if (now_ms >= screen->wake_ms && now_ms - screen->wake_ms <= JELLI_SCREEN_DOUBLE_TAP_MS)
        return true;
    screen->wake_guard = false;
    return false;
}
