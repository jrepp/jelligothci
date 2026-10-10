#include "player.h"
#include "audio.h"
#include "voice.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

/* Each queued item is one reply frame behind the number of its reply, so frames of a
 * reply that is no longer current are dropped as they come up: no flush can race the
 * next reply's first frames. */
enum {
    LEAD_BYTES = JELLI_LOKI_RATE * 300 / 1000 * 2,
    RING_BYTES = 512 * 1024, /* About 10 s of 24 kHz mono. */
    HEADER = sizeof(uint32_t)
};

static const char *TAG = "agent_player";
static RingbufHandle_t ring;
static TaskHandle_t task;
/* The reply being received and played, the last one the bridge ended, and one refused
 * while the user held to talk, which the player task answers; 0 is none. */
static atomic_uint current, ended, refused;
static atomic_uint queued_bytes, dropped_bytes;

static void wake(void)
{
    if (task)
        xTaskNotifyGive(task);
}

/* WebSocket task: these never block. */
static void push(uint32_t reply, const uint8_t *samples, size_t size)
{
    uint8_t *item = NULL;
    if (reply != atomic_load(&current) ||
        xRingbufferSendAcquire(ring, (void **)&item, HEADER + size, 0) != pdTRUE) {
        atomic_fetch_add(&dropped_bytes, (unsigned)size);
        return;
    }
    memcpy(item, &reply, HEADER);
    memcpy(item + HEADER, samples, size);
    (void)xRingbufferSendComplete(ring, item);
    atomic_fetch_add(&queued_bytes, (unsigned)size); /* Counted once it can be read. */
    wake();
}

/* The board never speaks while the user holds to talk: a reply that begins then is
 * not played, its frames are dropped as they arrive, and the player task tells the
 * bridge (voice.c sets holding before it stops the player). */
static void begin(uint32_t reply)
{
    unsigned expected = reply;
    if (!jelli_agent_voice_holding()) {
        atomic_store(&dropped_bytes, 0u);
        atomic_store(&ended, 0u);
        atomic_store(&current, reply);
        /* A press between the check and the store found nothing to stop; take the reply
         * back unless that press's stop already did (and answered for it). */
        if (!jelli_agent_voice_holding() ||
            !atomic_compare_exchange_strong(&current, &expected, 0u)) {
            wake();
            return;
        }
    }
    atomic_store(&refused, reply);
    wake();
}

static void end(uint32_t reply)
{
    atomic_store(&ended, reply);
    wake();
}

static void played(uint32_t reply, bool complete)
{
    JelliLokiOut message = {.type = JELLI_LOKI_PLAYED, .reply = reply, .complete = complete};
    (void)jelli_agent_link_send(&message);
}

void jelli_agent_player_stop(void)
{
    unsigned reply = atomic_exchange(&current, 0u);
    if (reply) {
        ESP_LOGI(TAG, "Reply %u stopped", reply);
        played(reply, false);
    }
    wake();
}

bool jelli_agent_player_busy(void) { return atomic_load(&current) != 0u; }

/* Plays one queued frame, or drops it when its reply is no longer current. */
static void play_item(void)
{
    size_t size = 0u;
    uint8_t *item = xRingbufferReceive(ring, &size, 0);
    if (!item)
        return;
    uint32_t reply;
    memcpy(&reply, item, HEADER);
    size_t bytes = size - HEADER;
    if (reply == atomic_load(&current))
        (void)jelli_agent_audio_write((const int16_t *)(item + HEADER), bytes / sizeof(int16_t));
    atomic_fetch_sub(&queued_bytes, (unsigned)bytes);
    vRingbufferReturnItem(ring, item);
}

/* Plays what it can of the current reply; returns when it must wait for more. */
static void serve(uint32_t *started)
{
    for (;;) {
        unsigned reply = atomic_load(&current);
        bool reply_ended = reply && atomic_load(&ended) == reply;
        if (!reply) {
            while (atomic_load(&queued_bytes) && !atomic_load(&current))
                play_item(); /* Drops the stopped reply's frames. */
            return;
        }
        if (*started != reply && atomic_load(&queued_bytes) < LEAD_BYTES && !reply_ended)
            return; /* Building the lead. */
        if (atomic_load(&queued_bytes)) {
            *started = reply;
            play_item();
        } else if (reply_ended && atomic_compare_exchange_strong(&current, &reply, 0u)) {
            played(reply, atomic_load(&dropped_bytes) == 0u);
            return;
        } else {
            return; /* More of this reply is on its way. */
        }
    }
}

static void player_task(void *unused)
{
    (void)unused;
    uint32_t started = 0u; /* The reply whose lead has been played. */
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        unsigned reply = atomic_exchange(&refused, 0u);
        if (reply) {
            ESP_LOGW(TAG, "Reply %u refused while listening", reply);
            played(reply, false);
        }
        serve(&started);
    }
}

static void stop_hook(void) { jelli_agent_player_stop(); }

/* The session is gone, so there is no one to tell. */
static void session_ended(void)
{
    if (atomic_exchange(&current, 0u))
        wake();
}

const JelliAgentPlayerHooks *jelli_agent_player_hooks(void)
{
    static const JelliAgentPlayerHooks hooks = {.reply_begin = begin,
                                                .reply_audio = push,
                                                .reply_end = end,
                                                .stop_audio = stop_hook,
                                                .session_ended = session_ended};
    return &hooks;
}

bool jelli_agent_player_start(void)
{
    static StaticTask_t task_control;
    static StackType_t task_stack[4096u / sizeof(StackType_t)];
    ring = xRingbufferCreateWithCaps(RING_BYTES, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM);
    if (!ring)
        return false;
    task = xTaskCreateStaticPinnedToCore(player_task, "agent_play", sizeof(task_stack), NULL, 6u,
                                         task_stack, &task_control, 0);
    return task != NULL;
}
