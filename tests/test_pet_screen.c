/* Screen sleep policy: idle timer, double-tap wake, and clock edge cases. */
#include "jelli/pet_screen.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static JelliPetScreen asleep_at(uint64_t now)
{
    JelliPetScreen screen;
    jelli_pet_screen_init(&screen, now);
    jelli_pet_screen_set(&screen, true, now);
    return screen;
}

static void idle_timer(void)
{
    JelliPetScreen screen;
    jelli_pet_screen_init(&screen, 1000u);
    CHECK(!screen.asleep && jelli_pet_screen_remaining(&screen, 1000u) == JELLI_SCREEN_SLEEP_MS);
    CHECK(!jelli_pet_screen_idle(&screen, 1000u + 179999u));
    CHECK(jelli_pet_screen_remaining(&screen, 1000u + 179999u) == 1u);
    CHECK(jelli_pet_screen_idle(&screen, 1000u + 180000u) && screen.asleep);
    CHECK(!jelli_pet_screen_idle(&screen, 1000u + 180001u)); /* Edge fires once. */
    CHECK(jelli_pet_screen_remaining(&screen, 1000u + 180001u) == 0u);
    /* Activity restarts the timer from its own time. */
    jelli_pet_screen_init(&screen, 0u);
    jelli_pet_screen_activity(&screen, 100000u);
    CHECK(!jelli_pet_screen_idle(&screen, 279999u));
    CHECK(jelli_pet_screen_remaining(&screen, 200000u) == 80000u);
    CHECK(jelli_pet_screen_idle(&screen, 280000u));
    /* A clock that never advances never sleeps. */
    jelli_pet_screen_init(&screen, 5u);
    for (unsigned i = 0u; i < 100000u; ++i)
        CHECK(!jelli_pet_screen_idle(&screen, 5u));
    /* Waking by force restarts the timer; forcing sleep is immediate. */
    jelli_pet_screen_set(&screen, false, 7000u);
    CHECK(jelli_pet_screen_remaining(&screen, 7000u) == JELLI_SCREEN_SLEEP_MS);
    jelli_pet_screen_set(&screen, true, 7001u);
    CHECK(screen.asleep && jelli_pet_screen_remaining(&screen, 7001u) == 0u);
}

static void clock_edges(void)
{
    JelliPetScreen screen;
    /* A regressing clock re-anchors rather than underflowing into a sleep. */
    jelli_pet_screen_init(&screen, 1000000u);
    CHECK(jelli_pet_screen_remaining(&screen, 500u) == JELLI_SCREEN_SLEEP_MS);
    CHECK(!jelli_pet_screen_idle(&screen, 500u) && screen.activity_ms == 500u);
    CHECK(!jelli_pet_screen_idle(&screen, 500u + 179999u));
    CHECK(jelli_pet_screen_idle(&screen, 500u + 180000u));
    /* A large forward jump (host resume) is idle time like any other. */
    jelli_pet_screen_init(&screen, 0u);
    CHECK(jelli_pet_screen_idle(&screen, UINT64_C(864000000)));
    /* Values near UINT64_MAX never wrap. */
    jelli_pet_screen_init(&screen, UINT64_MAX - 100u);
    CHECK(jelli_pet_screen_remaining(&screen, UINT64_MAX) == JELLI_SCREEN_SLEEP_MS - 100u);
    CHECK(!jelli_pet_screen_idle(&screen, UINT64_MAX));
    jelli_pet_screen_init(&screen, UINT64_MAX - JELLI_SCREEN_SLEEP_MS);
    CHECK(jelli_pet_screen_idle(&screen, UINT64_MAX));
    jelli_pet_screen_init(&screen, 0u);
    CHECK(jelli_pet_screen_remaining(&screen, UINT64_MAX) == 0u);
    CHECK(jelli_pet_screen_idle(&screen, UINT64_MAX));
    /* Wake taps across the top of the range. */
    screen = asleep_at(UINT64_MAX - 600u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, UINT64_MAX - 500u));
    CHECK(jelli_pet_screen_wake_tap(&screen, 10, 10, UINT64_MAX));
    CHECK(jelli_pet_screen_guarded(&screen, UINT64_MAX));
    /* A tap stamped before its predecessor restarts the window. */
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, 900u));
    CHECK(jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
}

static void double_tap_window(void)
{
    JelliPetScreen screen;
    jelli_pet_screen_init(&screen, 0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, 0u)); /* Awake: not a wake tap. */
    CHECK(!screen.tap_pending);
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1000u) && screen.asleep);
    CHECK(jelli_pet_screen_wake_tap(&screen, 100, 100, 1500u) && !screen.asleep);
    CHECK(jelli_pet_screen_remaining(&screen, 1500u) == JELLI_SCREEN_SLEEP_MS);
    screen = asleep_at(0u); /* Same instant: both taps in one host batch. */
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1000u));
    CHECK(jelli_pet_screen_wake_tap(&screen, 100, 100, 1000u));
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1000u));
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1501u) && screen.asleep);
    /* Three taps: the first stands alone; the window restarts from the second. */
    CHECK(jelli_pet_screen_wake_tap(&screen, 105, 95, 1900u) && !screen.asleep);
    /* A swipe between taps cancels the candidate. */
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1000u));
    jelli_pet_screen_cancel_tap(&screen);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1100u) && screen.asleep);
    CHECK(jelli_pet_screen_wake_tap(&screen, 100, 100, 1200u));
    /* Falling asleep forgets a candidate tap. */
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1000u));
    jelli_pet_screen_set(&screen, true, 1100u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 100, 100, 1200u) && screen.asleep);
}

static bool pair_wakes(int ax, int ay, int bx, int by)
{
    JelliPetScreen screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, ax, ay, 10u));
    return jelli_pet_screen_wake_tap(&screen, bx, by, 20u);
}

static void double_tap_distance(void)
{
    CHECK(pair_wakes(100, 100, 180, 100) && pair_wakes(100, 100, 100, 20));
    CHECK(!pair_wakes(100, 100, 181, 100) && !pair_wakes(100, 100, 100, 19));
    CHECK(pair_wakes(100, 100, 156, 156));  /* 56^2 * 2 = 6272 <= 6400 */
    CHECK(!pair_wakes(100, 100, 157, 157)); /* 57^2 * 2 = 6498 > 6400 */
    CHECK(pair_wakes(-40, -40, 0, 0));      /* Off-surface coordinates are just numbers. */
    CHECK(!pair_wakes(INT_MIN, INT_MIN, INT_MAX, INT_MAX));
    CHECK(!pair_wakes(INT_MAX, 0, INT_MIN, 0) && !pair_wakes(0, INT_MIN, 0, INT_MAX));
    CHECK(pair_wakes(INT_MAX, INT_MAX, INT_MAX - 50, INT_MAX - 50));
    CHECK(pair_wakes(INT_MIN, INT_MIN, INT_MIN + 80, INT_MIN));
}

static void wake_guard(void)
{
    JelliPetScreen screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_guarded(&screen, 0u));
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
    CHECK(jelli_pet_screen_wake_tap(&screen, 10, 10, 1200u));
    CHECK(jelli_pet_screen_guarded(&screen, 1200u) && jelli_pet_screen_guarded(&screen, 1700u));
    CHECK(!jelli_pet_screen_guarded(&screen, 1701u));
    CHECK(!jelli_pet_screen_guarded(&screen, 1300u)); /* Expired guards stay cleared. */
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
    CHECK(jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
    CHECK(!jelli_pet_screen_guarded(&screen, 999u)); /* Regressing clock drops the guard. */
    screen = asleep_at(0u);
    CHECK(!jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
    CHECK(jelli_pet_screen_wake_tap(&screen, 10, 10, 1000u));
    jelli_pet_screen_set(&screen, false, 1100u); /* A forced wake never guards. */
    CHECK(!jelli_pet_screen_guarded(&screen, 1100u));
}

static void null_safety(void)
{
    jelli_pet_screen_init(NULL, 0u);
    jelli_pet_screen_activity(NULL, 0u);
    jelli_pet_screen_set(NULL, true, 0u);
    jelli_pet_screen_cancel_tap(NULL);
    CHECK(!jelli_pet_screen_idle(NULL, UINT64_MAX));
    CHECK(jelli_pet_screen_remaining(NULL, 0u) == 0u);
    CHECK(!jelli_pet_screen_wake_tap(NULL, 0, 0, 0u));
    CHECK(!jelli_pet_screen_guarded(NULL, 0u));
}

int main(void)
{
    idle_timer();
    clock_edges();
    double_tap_window();
    double_tap_distance();
    wake_guard();
    null_safety();
    printf("Screen sleep policy checks passed; state=%zu bytes\n", sizeof(JelliPetScreen));
    return 0;
}
