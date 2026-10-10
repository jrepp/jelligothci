#include "commands_setup.h"
#include "audio.h"
#include "settings.h"
#include "wifi.h"
#include "jelli/agent_args.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1 kHz at -12 dBFS: 24 samples per period at 24 kHz. Tested in 20 ms blocks. */
enum { TONE_PERIOD = 24, TEST_BLOCK = JELLI_AGENT_AUDIO_RATE / 50, TEST_SKIP = 5 };

static const char *TAG = "agent_setup";
static bool audio_ready, wifi_ready;
static int16_t tone[TEST_BLOCK];
static int16_t heard[TEST_BLOCK * JELLI_AGENT_AUDIO_CHANNELS];
_Static_assert(JELLI_AGENT_AUDIO_CHANNELS == 4u, "the test reports four slots");

static void reply(char *out, size_t capacity, const char *body)
{
    (void)snprintf(out, capacity, "%s", body);
}

/* Not optimized away, unlike a plain memset before a buffer goes out of scope. */
static void wipe(void *data, size_t size)
{
    volatile uint8_t *bytes = data;
    while (size--)
        *bytes++ = 0u;
}

bool jelli_agent_setup_audio_ready(void) { return audio_ready; }

bool jelli_agent_setup_wifi_ready(void) { return wifi_ready; }

void jelli_agent_setup_start(void)
{
    for (unsigned i = 0u; i < TEST_BLOCK; ++i)
        tone[i] = (int16_t)lrintf(8231.0f *
                                  sinf(6.2831853f * (float)(i % TONE_PERIOD) / (float)TONE_PERIOD));
    bool settings = jelli_agent_settings_open();
    wifi_ready = settings && jelli_agent_wifi_start();
    audio_ready = jelli_agent_audio_start();
    ESP_LOGI(TAG, "Setup: settings=%d wifi=%d audio=%d", settings, wifi_ready, audio_ready);
}

static void wifi_command(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->words[1];
    if (!strcmp(verb, "status") && request->count == 2u) {
        jelli_agent_wifi_status(out, capacity);
    } else if (!strcmp(verb, "clear") && request->count == 2u) {
        reply(out, capacity,
              jelli_agent_wifi_clear() ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"storage\"}");
    } else if (!strcmp(verb, "set") && (request->count == 3u || request->count == 4u)) {
        uint8_t ssid[32], password[63];
        size_t ssid_size = 0u, password_size = 0u;
        bool ok =
            jelli_agent_hex_decode(request->words[2], ssid, sizeof(ssid), &ssid_size) &&
            (request->count == 3u || jelli_agent_hex_decode(request->words[3], password,
                                                            sizeof(password), &password_size)) &&
            jelli_agent_wifi_set(ssid, ssid_size, password, password_size);
        wipe(password, sizeof(password));
        reply(out, capacity,
              ok ? "{\"ok\":true,\"wifi\":\"saved\"}"
                 : "{\"ok\":false,\"error\":\"wifi_credentials\"}");
    } else {
        reply(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
    }
}

typedef struct {
    double energy[JELLI_AGENT_AUDIO_CHANNELS], tone[JELLI_AGENT_AUDIO_CHANNELS];
    int peak[JELLI_AGENT_AUDIO_CHANNELS];
} Levels;

/* Adds one block per slot: energy, peak, and the 1 kHz share by Goertzel. A block is
 * exactly 20 tone periods, so a pure tone scores 1.0 of its energy. */
static void measure(Levels *levels)
{
    static const float coefficient = 1.9318517f; /* 2 cos(2 pi 1000 / 24000) */
    for (unsigned channel = 0u; channel < JELLI_AGENT_AUDIO_CHANNELS; ++channel) {
        float s1 = 0.0f, s2 = 0.0f;
        double energy = 0.0;
        for (unsigned i = 0u; i < TEST_BLOCK; ++i) {
            int value = heard[i * JELLI_AGENT_AUDIO_CHANNELS + channel];
            float s0 = (float)value + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
            energy += (double)value * value;
            if (abs(value) > levels->peak[channel])
                levels->peak[channel] = abs(value);
        }
        double power = (double)s1 * s1 + (double)s2 * s2 - (double)coefficient * s1 * s2;
        levels->energy[channel] += energy;
        levels->tone[channel] += 2.0 * power / TEST_BLOCK;
    }
}

/* Plays the tone while capturing every slot, so the echo reference should carry it
 * almost purely and the microphones acoustically, mixed with room noise. */
static void audio_test(uint32_t ms, char *out, size_t capacity)
{
    Levels levels = {{0.0}, {0.0}, {0}};
    unsigned blocks = ms / 20u, counted = 0u;
    for (unsigned block = 0u; block < blocks; ++block) {
        if (!jelli_agent_audio_write(tone, TEST_BLOCK) ||
            !jelli_agent_audio_read(heard, TEST_BLOCK)) {
            reply(out, capacity, "{\"ok\":false,\"error\":\"audio_io\"}");
            return;
        }
        if (block >= TEST_SKIP) { /* Skip DMA start-up. */
            ++counted;
            measure(&levels);
        }
    }
    double samples = counted ? (double)counted * TEST_BLOCK : 1.0;
    long rms[JELLI_AGENT_AUDIO_CHANNELS], tone_pct[JELLI_AGENT_AUDIO_CHANNELS];
    for (unsigned c = 0u; c < JELLI_AGENT_AUDIO_CHANNELS; ++c) {
        rms[c] = lrint(sqrt(levels.energy[c] / samples));
        tone_pct[c] = levels.energy[c] > 0.0 ? lrint(100.0 * levels.tone[c] / levels.energy[c]) : 0;
    }
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"blocks\":%u,\"slots\":[\"MIC1\",\"MIC3\",\"MIC2\",\"MIC4\"],"
                   "\"rms\":[%ld,%ld,%ld,%ld],\"peak\":[%d,%d,%d,%d],"
                   "\"tone_pct\":[%ld,%ld,%ld,%ld]}",
                   counted, rms[0], rms[1], rms[2], rms[3], levels.peak[0], levels.peak[1],
                   levels.peak[2], levels.peak[3], tone_pct[0], tone_pct[1], tone_pct[2],
                   tone_pct[3]);
}

static void audio_command(const JelliAgentRequest *request, char *out, size_t capacity)
{
    uint32_t value = 0u;
    bool number = request->count == 3u && jelli_agent_number(request->words[2], &value);
    if (!audio_ready)
        reply(out, capacity, "{\"ok\":false,\"error\":\"audio_unavailable\"}");
    else if (number && !strcmp(request->words[1], "volume"))
        reply(out, capacity,
              jelli_agent_audio_volume(value) ? "{\"ok\":true}"
                                              : "{\"ok\":false,\"error\":\"range\"}");
    else if (number && !strcmp(request->words[1], "test") && value >= 200u && value <= 5000u)
        audio_test(value, out, capacity);
    else
        reply(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
}

bool jelli_agent_command_setup(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->words[0];
    if (request->count < 2u)
        return false;
    if (!strcmp(verb, "wifi"))
        wifi_command(request, out, capacity);
    else if (!strcmp(verb, "audio"))
        audio_command(request, out, capacity);
    else
        return false;
    return true;
}
