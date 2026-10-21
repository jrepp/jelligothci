#include "sound_output.h"
#include "audio_board.h"
#include "jelli/sound.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

typedef struct {
    uint8_t cue, volume;
} Request;
static QueueHandle_t queue, music_queue;
static StaticQueue_t queue_control, music_queue_control;
static uint8_t queue_storage[4u * sizeof(Request)];
static uint8_t music_queue_storage[4u * sizeof(JelliMusicCommand)];
static StaticTask_t task_control;
static StackType_t task_stack[4096u / sizeof(StackType_t)];
static TaskHandle_t worker;
static esp_codec_dev_handle_t codec;
static int16_t pcm[JELLI_SOUND_BLOCK], cue_pcm[JELLI_SOUND_BLOCK];
static JelliSynth synth;
static JelliMusic music;
static JelliMusicTransition transition;
static unsigned cue_gain;
static StaticSemaphore_t audio_mutex_storage;
static SemaphoreHandle_t audio_mutex;
static bool audio_enabled;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static JelliMusicStatus published;

static void publish(void)
{
    portENTER_CRITICAL(&status_lock);
    published = jelli_music_status(&music);
    portEXIT_CRITICAL(&status_lock);
}

bool jelli_sound_output_enable(bool enabled)
{
    if (!audio_mutex || xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
        return false;
    int result = ESP_CODEC_DEV_OK;
    if (enabled != audio_enabled) {
        esp_codec_dev_sample_info_t format = {
            .sample_rate = JELLI_SOUND_RATE, .channel = 1, .bits_per_sample = 16};
        result = enabled ? esp_codec_dev_open(codec, &format) : esp_codec_dev_close(codec);
        if (result == ESP_CODEC_DEV_OK)
            audio_enabled = enabled;
    }
    xSemaphoreGive(audio_mutex);
    if (worker)
        xTaskNotifyGive(worker);
    return result == ESP_CODEC_DEV_OK;
}

/* Audio task exclusively owns music/synth; controls apply between PCM blocks.
 * A bounded control queue and independent care queue prevent record commands
 * from waiting for an entire care cue. Hardware mutex covers one block only. */
static void commands(void)
{
    JelliMusicCommand command;
    for (unsigned i = 0; i < 4u; ++i) {
        if (xQueueReceive(music_queue, &command, 0) != pdTRUE)
            break;
        if (jelli_music_control(&music, command))
            jelli_music_transition_begin(&transition);
    }
    if (!synth.playing) {
        Request request;
        if (xQueueReceive(queue, &request, 0) == pdTRUE) {
            cue_gain = request.volume;
            (void)jelli_sound_start(&synth, request.cue);
        }
    }
}

static bool submit_block(void)
{
    JelliMusic before = music;
    bool duck = synth.playing;
    memset(pcm, 0, sizeof(pcm));
    memset(cue_pcm, 0, sizeof(cue_pcm));
    (void)jelli_music_render(&music, pcm, JELLI_SOUND_BLOCK);
    (void)jelli_sound_render(&synth, cue_pcm, JELLI_SOUND_BLOCK);
    for (unsigned i = 0; i < JELLI_SOUND_BLOCK; ++i)
        pcm[i] = jelli_music_mix(jelli_music_transition_sample(&transition, pcm[i]), cue_pcm[i],
                                 cue_gain, duck);
    if (esp_codec_dev_write(codec, pcm, sizeof(pcm)) == ESP_CODEC_DEV_OK)
        return true;
    music = before; /* Never credit samples rejected by the output device. */
    music.playing = false;
    synth.playing = false;
    transition = (JelliMusicTransition){0};
    ESP_LOGW("sound", "PCM submission failed; playback paused");
    return false;
}

static void sound_task(void *unused)
{
    (void)unused;
    for (;;) {
        if (xSemaphoreTake(audio_mutex, portMAX_DELAY) != pdTRUE)
            continue;
        bool was_active = music.playing || synth.playing;
        commands();
        if (!audio_enabled) {
            music.playing = false;
            synth.playing = false;
        }
        bool active = music.playing || synth.playing || (audio_enabled && transition.remaining);
        if (active)
            (void)submit_block();
        bool idle = !music.playing && !synth.playing;
        if (idle && audio_enabled && (active || was_active)) {
            /* Flush the DMA tail after pause/stop/completion rather than leaving
             * the last partly filled note block available for repetition. */
            memset(pcm, 0, sizeof(pcm));
            (void)esp_codec_dev_write(codec, pcm, sizeof(pcm));
        }
        publish();
        xSemaphoreGive(audio_mutex);
        if (idle && !uxQueueMessagesWaiting(queue) && !uxQueueMessagesWaiting(music_queue))
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

bool jelli_sound_output_init(void)
{
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    codec = jelli_audio_board_init();
    if (!codec)
        return false;
    esp_codec_dev_sample_info_t format = {
        .sample_rate = JELLI_SOUND_RATE, .channel = 1, .bits_per_sample = 16};
    /* Keep the existing maximum codec ceiling; master and cue gains are mixed
     * in PCM so care feedback and the record can share one output stream. */
    if (esp_codec_dev_set_out_vol(codec, 80) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK)
        return false;
    audio_mutex = xSemaphoreCreateMutexStatic(&audio_mutex_storage);
    audio_enabled = true;
    queue = xQueueCreateStatic(4u, sizeof(Request), queue_storage, &queue_control);
    music_queue = xQueueCreateStatic(4u, sizeof(JelliMusicCommand), music_queue_storage,
                                     &music_queue_control);
    if (!audio_mutex || !queue || !music_queue)
        return false;
    worker = xTaskCreateStatic(sound_task, "jelli_sound", sizeof(task_stack), NULL, 3u, task_stack,
                               &task_control);
    if (!worker)
        return false;
    size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI("sound", "ready PCM16 22050 Hz; heap delta=%u free=%u; bounded record/care mixer",
             (unsigned)(before > after ? before - after : 0u), (unsigned)after);
    return true;
}

bool jelli_sound_output_request(void *ctx, unsigned cue, unsigned volume)
{
    (void)ctx;
    if (!queue || !worker || cue >= JELLI_SOUND_COUNT || volume > 80u)
        return false;
    Request request = {(uint8_t)cue, (uint8_t)volume};
    bool accepted = xQueueSend(queue, &request, 0) == pdTRUE;
    if (accepted)
        xTaskNotifyGive(worker);
    return accepted;
}

bool jelli_music_output_control(void *ctx, JelliMusicCommand command)
{
    (void)ctx;
    JelliMusic check = {0};
    if (!music_queue || !worker || !jelli_music_control(&check, command))
        return false;
    bool accepted = xQueueSend(music_queue, &command, 0) == pdTRUE;
    if (accepted)
        xTaskNotifyGive(worker);
    return accepted;
}

bool jelli_music_output_status(void *ctx, JelliMusicStatus *status)
{
    (void)ctx;
    if (!worker || !status)
        return false;
    portENTER_CRITICAL(&status_lock);
    *status = published;
    portEXIT_CRITICAL(&status_lock);
    return true;
}
