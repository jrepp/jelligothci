#include "../ports/esp32/main/case_button_state.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    JelliCaseButtonState held = {0};
    CHECK(!jelli_case_button_sample(&held, true, true, 0));
    CHECK(!jelli_case_button_sample(&held, true, false, 50));
    CHECK(!jelli_case_button_sample(&held, true, false, 90));
    JelliCaseButtonState s = {0};
    CHECK(!jelli_case_button_sample(&s, true, false, 0));
    CHECK(!jelli_case_button_sample(&s, true, true, 10));
    CHECK(!jelli_case_button_sample(&s, true, false, 20));
    CHECK(!jelli_case_button_sample(&s, true, true, 30));
    CHECK(!jelli_case_button_sample(&s, true, true, 70));
    CHECK(!jelli_case_button_sample(&s, true, false, 100));
    CHECK(jelli_case_button_sample(&s, true, false, 140));
    CHECK(!jelli_case_button_sample(&s, true, true, 200));
    CHECK(!jelli_case_button_sample(&s, true, true, 240));
    CHECK(!jelli_case_button_sample(&s, true, false, 1300));
    CHECK(!jelli_case_button_sample(&s, true, false, 1340));
    CHECK(!jelli_case_button_sample(&s, true, true, 1400));
    CHECK(!jelli_case_button_sample(&s, true, true, 1440));
    CHECK(!jelli_case_button_sample(&s, false, false, 1450));
    CHECK(!jelli_case_button_sample(&s, true, true, 1460));
    CHECK(!jelli_case_button_sample(&s, true, false, 1500));
    CHECK(!jelli_case_button_sample(&s, true, false, 1540));
    CHECK(!jelli_case_button_sample(&s, true, true, 1600));
    CHECK(!jelli_case_button_sample(&s, true, true, 1640));
    CHECK(!jelli_case_button_sample(&s, true, false, 1700));
    CHECK(jelli_case_button_sample(&s, true, false, 1740));
    return 0;
}
