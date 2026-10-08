#include "jelli/tunables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    JelliTunables store = {0};
    for (unsigned i = 0; i < JELLI_TUNE_COUNT; ++i) {
        const JelliTunableDefinition *d = jelli_tunable_definition((JelliTunable)i);
        JelliTunable key;
        CHECK(strchr(d->name, '.') != NULL);
        CHECK(jelli_tunable_find(d->name, &key) && (unsigned)key == i);
    }
    JelliTunable legacy;
    CHECK(jelli_tunable_find("idle_frame_ms", &legacy) && legacy == JELLI_TUNE_IDLE_MS);
    static const JelliTuningProfile profiles[] = {
        {.form = 1, .settings = {.mask = 1u << JELLI_TUNE_IDLE_MS, .values = {1200}}}};
    CHECK(jelli_tunables_register(&store, profiles, 1u));
    CHECK(jelli_tunable_get(&store, 1u, 0u, JELLI_TUNE_IDLE_MS) == 900u);
    CHECK(jelli_tunable_get(&store, 1u, 1u, JELLI_TUNE_IDLE_MS) == 1200u);
    CHECK(jelli_tunable_set(&store, 0u, JELLI_TUNE_IDLE_MS, 1500u));
    CHECK(jelli_tunable_set(&store, 1u, JELLI_TUNE_IDLE_MS, 1800u));
    CHECK(jelli_tunable_get(&store, 1u, 1u, JELLI_TUNE_IDLE_MS) == 1800u);
    CHECK(jelli_tunable_get(&store, 2u, 1u, JELLI_TUNE_IDLE_MS) == 1500u);
    JelliTunables before = store;
    CHECK(!jelli_tunable_set(&store, 1u, JELLI_TUNE_IDLE_MS, 0u));
    CHECK(!jelli_tunable_set(&store, 1u, JELLI_TUNE_COUNT, 10u));
    CHECK(store.revision == before.revision && store.profiles == before.profiles &&
          store.profile_count == before.profile_count);
    for (unsigned key = 0; key < JELLI_TUNE_COUNT; ++key)
        for (unsigned id = 0; id <= JELLI_TUNING_PETS; ++id)
            CHECK(jelli_tunable_get(&store, id, 1u, (JelliTunable)key) ==
                  jelli_tunable_get(&before, id, 1u, (JelliTunable)key));
    CHECK(jelli_tunable_reset(&store, 1u, JELLI_TUNE_IDLE_MS));
    CHECK(jelli_tunable_reset(&store, 0u, JELLI_TUNE_IDLE_MS));
    CHECK(jelli_tunable_get(&store, 1u, 1u, JELLI_TUNE_IDLE_MS) == 1200u);
    for (unsigned i = 1; i <= JELLI_TUNING_PETS; ++i)
        CHECK(jelli_tunable_set(&store, i, JELLI_TUNE_IDLE_MS, 1000u));
    CHECK(!jelli_tunable_set(&store, 99u, JELLI_TUNE_IDLE_MS, 1000u));
    CHECK(jelli_tunable_reset(&store, 1u, JELLI_TUNE_IDLE_MS));
    CHECK(jelli_tunable_set(&store, 99u, JELLI_TUNE_IDLE_MS, 1000u));
    JelliTuningProfile bad = {.form = 3, .settings = {.mask = 1, .values = {0}}};
    before = store;
    CHECK(!jelli_tunables_register(&store, &bad, 1u));
    CHECK(store.revision == before.revision && store.profiles == before.profiles &&
          store.profile_count == before.profile_count);
    for (unsigned key = 0; key < JELLI_TUNE_COUNT; ++key)
        for (unsigned id = 0; id <= JELLI_TUNING_PETS; ++id)
            CHECK(jelli_tunable_get(&store, id, 1u, (JelliTunable)key) ==
                  jelli_tunable_get(&before, id, 1u, (JelliTunable)key));
    puts("Tunables: profiles, scope precedence, invalid mutation atomicity, capacity and reuse "
         "passed.");
    return 0;
}
