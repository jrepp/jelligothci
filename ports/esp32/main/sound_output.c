#include "sound_output.h"
#include "jelli/sound.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

typedef struct {
    uint8_t cue, volume;
} Request;
static QueueHandle_t queue;
static StaticQueue_t queue_control;
static uint8_t queue_storage[4u * sizeof(Request)];
static StaticTask_t task_control;
static StackType_t task_stack[4096u / sizeof(StackType_t)];
static esp_codec_dev_handle_t codec;
static int16_t pcm[JELLI_SOUND_BLOCK];
static JelliSynth synth;

static void sound_task(void *unused)
{
    (void)unused;
    for (;;) {
        Request request;
        if (xQueueReceive(queue, &request, portMAX_DELAY) != pdTRUE)
            continue;
        bool ok = esp_codec_dev_set_out_vol(codec, request.volume) == ESP_CODEC_DEV_OK;
        ok = ok && jelli_sound_start(&synth, request.cue);
        unsigned samples = 0u;
        while (ok && synth.playing) {
            size_t count = jelli_sound_render(&synth, pcm, JELLI_SOUND_BLOCK);
            if (!count) {
                ok = false;
                break;
            }
            ok = esp_codec_dev_write(codec, pcm, (int)(count * sizeof(pcm[0]))) == ESP_CODEC_DEV_OK;
            samples += (unsigned)count;
        }
        /* Drain the final note into silence rather than repeating a DMA tail. */
        memset(pcm, 0, sizeof(pcm));
        if (ok)
            ok = esp_codec_dev_write(codec, pcm, sizeof(pcm)) == ESP_CODEC_DEV_OK;
        ESP_LOGI("sound", "%s cue=%s volume=%u samples=%u stack_free=%u",
                 ok ? "submitted" : "failed", jelli_sound_name(request.cue),
                 (unsigned)request.volume, samples, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

bool jelli_sound_output_init(void)
{
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    codec = bsp_audio_codec_speaker_init();
    if (!codec)
        return false;
    esp_codec_dev_sample_info_t format = {
        .sample_rate = JELLI_SOUND_RATE, .channel = 1, .bits_per_sample = 16};
    if (esp_codec_dev_set_out_vol(codec, 35) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK)
        return false;
    queue = xQueueCreateStatic(4u, sizeof(Request), queue_storage, &queue_control);
    if (!queue || !xTaskCreateStatic(sound_task, "jelli_sound", sizeof(task_stack), NULL, 3u,
                                     task_stack, &task_control))
        return false;
    size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI("sound",
             "ready mono PCM16 22050 Hz; internal heap delta=%u free=%u; menu and CLI cues enabled",
             (unsigned)(before > after ? before - after : 0u), (unsigned)after);
    return true;
}

bool jelli_sound_output_request(void *ctx, unsigned cue, unsigned volume)
{
    (void)ctx;
    if (!queue || cue >= JELLI_SOUND_COUNT || volume > 80u)
        return false;
    Request request = {(uint8_t)cue, (uint8_t)volume};
    return xQueueSend(queue, &request, 0) == pdTRUE;
}
