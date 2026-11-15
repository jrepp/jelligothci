#include "../ports/esp32/main/auto_sleep_policy.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    CHECK(jelli_auto_sleep_allowed(true, 0u, true, true, false));
    CHECK(!jelli_auto_sleep_allowed(false, 0u, true, true, false));
    CHECK(!jelli_auto_sleep_allowed(true, 0x20u, true, true, false));
    CHECK(!jelli_auto_sleep_allowed(true, 0u, false, true, false));
    CHECK(!jelli_auto_sleep_allowed(true, 0u, true, false, false));
    CHECK(!jelli_auto_sleep_allowed(true, 0u, true, true, true));
    CHECK(!jelli_auto_sleep_should_wake(true, true, true, false, false));
    CHECK(jelli_auto_sleep_should_wake(true, true, true, true, false));
    CHECK(jelli_auto_sleep_should_wake(true, true, true, false, true));
    CHECK(jelli_auto_sleep_should_wake(true, false, true, false, false));
    CHECK(!jelli_auto_sleep_should_wake(false, false, true, true, true));
    CHECK(!jelli_auto_sleep_idle(59999u, 0u));
    CHECK(jelli_auto_sleep_idle(60000u, 0u));
    CHECK(!jelli_auto_sleep_idle(10u, 11u));
    return 0;
}
