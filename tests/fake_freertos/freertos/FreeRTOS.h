#ifndef JELLI_TESTS_FAKE_FREERTOS_H
#define JELLI_TESTS_FAKE_FREERTOS_H

/* A host stand-in for the little of FreeRTOS that ports/esp32-agent/main/loki_models.c
 * uses: its spinlock, here a pthread mutex, so tests can race the slot with real
 * threads (tests/test_agent_loki_models_slot.c). Not for any other port file. */
#include <pthread.h>

typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define taskENTER_CRITICAL(mux) ((void)pthread_mutex_lock(mux))
#define taskEXIT_CRITICAL(mux) ((void)pthread_mutex_unlock(mux))

#endif
