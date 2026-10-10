#include "voice.h"
#include "audio.h"
#include "loki_link.h"
#include "player.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

enum {
    FRAME_SAMPLES = JELLI_LOKI_RATE * 60 / 1000, /* 1,440 samples, 2,880 bytes. */
    QUEUE_FRAMES = 64,                           /* About 3.8 s of slack for slow sends. */
    MAX_FRAMES = 30 * 1000 / 60                  /* 30 s per utterance. */
};

typedef struct {
    uint32_t stream; /* 0 marks the end of the utterance. */
    uint32_t seq;
    int16_t samples[FRAME_SAMPLES];
} Frame;

static const char *TAG = "agent_voice";
static QueueHandle_t queue;
static TaskHandle_t capture_task;
/* The utterance being held, 0 when none; stream numbers only rise. open is the
 * utterance whose ptt_end has not gone yet: a new one waits for it, since the bridge
 * allows one open stream at a time. */
static atomic_uint holding, open_stream, last_stream, dropped;
/* Capture task only. */
static int16_t tdm[FRAME_SAMPLES * JELLI_AGENT_AUDIO_CHANNELS];
static Frame captured;

bool jelli_agent_voice_holding(void) { return atomic_load(&holding) != 0u; }

/* Holding is set before the player stops, so no reply can begin in between. */
bool jelli_agent_voice_begin(void)
{
    unsigned none = 0u;
    if (jelli_agent_voice_holding() || atomic_load(&open_stream) || !jelli_agent_link_online())
        return false;
    unsigned stream = atomic_fetch_add(&last_stream, 1u) + 1u;
    if (!atomic_compare_exchange_strong(&holding, &none, stream))
        return false;
    jelli_agent_player_stop();
    JelliLokiOut start = {.type = JELLI_LOKI_PTT_START, .stream = stream};
    if (!jelli_agent_link_send(&start)) {
        atomic_store(&holding, 0u);
        return false;
    }
    atomic_store(&open_stream, stream);
    atomic_store(&dropped, 0u);
    xTaskNotifyGive(capture_task);
    return true;
}

void jelli_agent_voice_end(void) { atomic_store(&holding, 0u); }

/* MIC1 is the first of the ES7210's four TDM slots (memo-025). */
static void take_mic1(void)
{
    for (unsigned i = 0u; i < FRAME_SAMPLES; ++i)
        captured.samples[i] = tdm[i * JELLI_AGENT_AUDIO_CHANNELS];
}

static void capture(void *unused)
{
    (void)unused;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* The open stream, not holding: a release that came before this task woke still
         * gets its end marker, so ptt_end goes and the next press is not refused. */
        unsigned stream = atomic_load(&open_stream);
        if (!stream)
            continue;
        /* The DMA holds audio from before the press; read it away first. */
        (void)jelli_agent_audio_read(tdm, FRAME_SAMPLES);
        for (uint32_t seq = 0u; atomic_load(&holding) == stream && seq < MAX_FRAMES; ++seq) {
            if (!jelli_agent_audio_read(tdm, FRAME_SAMPLES))
                break;
            take_mic1();
            captured.stream = stream;
            captured.seq = seq;
            if (xQueueSend(queue, &captured, 0) != pdTRUE)
                atomic_fetch_add(&dropped, 1u);
        }
        unsigned expected = stream;
        (void)atomic_compare_exchange_strong(&holding, &expected, 0u);
        captured.stream = 0u; /* The end marker, which waits for room. */
        captured.seq = stream;
        (void)xQueueSend(queue, &captured, portMAX_DELAY);
    }
}

static void send_frame(const Frame *frame, bool final)
{
    JelliLokiAudio audio = {
        .kind = JELLI_LOKI_MIC, .final = final, .stream = frame->stream, .seq = frame->seq};
    (void)jelli_agent_link_send_audio(&audio, frame->samples, FRAME_SAMPLES);
}

/* Holds one frame back, so the last of an utterance can carry the final flag. */
static void sender(void *unused)
{
    (void)unused;
    static Frame next, held;
    bool have_held = false;
    uint32_t sent = 0u;
    for (;;) {
        if (xQueueReceive(queue, &next, portMAX_DELAY) != pdTRUE)
            continue;
        if (next.stream) {
            if (have_held) {
                send_frame(&held, false);
                ++sent;
            }
            held = next;
            have_held = true;
            continue;
        }
        if (have_held) {
            send_frame(&held, true);
            ++sent;
        }
        JelliLokiOut end = {.type = JELLI_LOKI_PTT_END,
                            .stream = next.seq,
                            .frames = sent,
                            .dropped = atomic_load(&dropped)};
        (void)jelli_agent_link_send(&end);
        atomic_store(&open_stream, 0u);
        ESP_LOGI(TAG, "Utterance %u: %u frames sent, %u dropped", (unsigned)next.seq,
                 (unsigned)sent, (unsigned)end.dropped);
        have_held = false;
        sent = 0u;
    }
}

bool jelli_agent_voice_start(void)
{
    static StaticTask_t capture_control, sender_control;
    static StackType_t capture_stack[4096u / sizeof(StackType_t)];
    static StackType_t sender_stack[4096u / sizeof(StackType_t)];
    queue = xQueueCreateWithCaps(QUEUE_FRAMES, sizeof(Frame), MALLOC_CAP_SPIRAM);
    if (!queue)
        return false;
    capture_task = xTaskCreateStaticPinnedToCore(capture, "agent_mic", sizeof(capture_stack), NULL,
                                                 7u, capture_stack, &capture_control, 1);
    return capture_task &&
           xTaskCreateStaticPinnedToCore(sender, "agent_send", sizeof(sender_stack), NULL, 5u,
                                         sender_stack, &sender_control, 0) != NULL;
}
