#include "audio.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

/* Speech that peaks near full scale plays at about unity gain. */
enum { DEFAULT_VOLUME = 20, MIC_GAIN_DB = 30 };

static const char *TAG = "agent_audio";
static i2s_chan_handle_t tx, rx;
static esp_codec_dev_handle_t speaker, microphone;

/* One I2S port: TX standard stereo for the ES8311, RX TDM with four slots for the
 * ES7210, sharing MCLK, BCLK, and WS (the layout of xiaozhi's BoxAudioCodec, MIT). */
static bool open_channels(void)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear_after_cb = true;
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(JELLI_AGENT_AUDIO_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = BSP_I2S_MCLK,
                     .bclk = BSP_I2S_SCLK,
                     .ws = BSP_I2S_LCLK,
                     .dout = BSP_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    i2s_tdm_config_t tdm = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(JELLI_AGENT_AUDIO_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {.mclk = BSP_I2S_MCLK,
                     .bclk = BSP_I2S_SCLK,
                     .ws = BSP_I2S_LCLK,
                     .dout = I2S_GPIO_UNUSED,
                     .din = BSP_I2S_DSIN},
    };
    tdm.clk_cfg.bclk_div = 8;
    /* Enabled like the BSP does; the codec library reconfigures slots when it opens. */
    return i2s_new_channel(&channel, &tx, &rx) == ESP_OK &&
           i2s_channel_init_std_mode(tx, &std) == ESP_OK &&
           i2s_channel_init_tdm_mode(rx, &tdm) == ESP_OK && i2s_channel_enable(tx) == ESP_OK &&
           i2s_channel_enable(rx) == ESP_OK;
}

static esp_codec_dev_handle_t new_device(esp_codec_dev_type_t type, const audio_codec_if_t *codec,
                                         const audio_codec_data_if_t *data)
{
    if (!codec || !data)
        return NULL;
    esp_codec_dev_cfg_t config = {.dev_type = type, .codec_if = codec, .data_if = data};
    return esp_codec_dev_new(&config);
}

/* Codec settings follow the board's BSP (bsp_audio_codec_speaker_init and
 * _microphone_init), except that all four ES7210 inputs run in TDM mode. */
static bool open_codecs(void)
{
    audio_codec_i2s_cfg_t i2s = {.port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s);
    audio_codec_i2c_cfg_t i2c = {
        .port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bsp_i2c_get_handle()};
    es8311_codec_cfg_t es8311 = {.ctrl_if = audio_codec_new_i2c_ctrl(&i2c),
                                 .gpio_if = audio_codec_new_gpio(),
                                 .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
                                 .pa_pin = BSP_POWER_AMP_IO,
                                 .use_mclk = true,
                                 .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f}};
    i2c.addr = ES7210_CODEC_DEFAULT_ADDR;
    es7210_codec_cfg_t es7210 = {.ctrl_if = audio_codec_new_i2c_ctrl(&i2c),
                                 .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 |
                                                 ES7210_SEL_MIC3 | ES7210_SEL_MIC4};
    if (!i2c.bus_handle || !es8311.ctrl_if || !es8311.gpio_if || !es7210.ctrl_if)
        return false;
    speaker = new_device(ESP_CODEC_DEV_TYPE_OUT, es8311_codec_new(&es8311), data);
    microphone = new_device(ESP_CODEC_DEV_TYPE_IN, es7210_codec_new(&es7210), data);
    return speaker && microphone;
}

/* The pet's calibrated output curve (ports/esp32/main/sound_output.c, memo-021). */
static bool set_curve(void)
{
    esp_codec_dev_vol_map_t points[] = {
        {0, -95.5f}, {1, -26.0f}, {2, -20.0f}, {3, -16.5f}, {5, -12.0f}, {10, -6.0f}, {18, -0.9f},
        {25, 2.0f},  {36, 5.1f},  {50, 8.0f},  {65, 10.3f}, {80, 12.1f}, {100, 14.0f}};
    esp_codec_dev_vol_curve_t curve = {points, (int)(sizeof(points) / sizeof(points[0]))};
    return esp_codec_dev_set_vol_curve(speaker, &curve) == ESP_CODEC_DEV_OK;
}

bool jelli_agent_audio_start(void)
{
    if (!open_channels() || !open_codecs() || !set_curve())
        return false;
    /* Mono playback; the library spreads it over the stereo slots. */
    esp_codec_dev_sample_info_t out = {
        .bits_per_sample = 16, .channel = 1, .sample_rate = JELLI_AGENT_AUDIO_RATE};
    /* All four TDM slots, in the ES7210's order MIC1, MIC3, MIC2, MIC4. */
    esp_codec_dev_sample_info_t in = {.bits_per_sample = 16,
                                      .channel = JELLI_AGENT_AUDIO_CHANNELS,
                                      .channel_mask = 0x0f,
                                      .sample_rate = JELLI_AGENT_AUDIO_RATE};
    bool ok = esp_codec_dev_open(speaker, &out) == ESP_CODEC_DEV_OK &&
              esp_codec_dev_open(microphone, &in) == ESP_CODEC_DEV_OK &&
              esp_codec_dev_set_in_channel_gain(microphone, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
                                                MIC_GAIN_DB) == ESP_CODEC_DEV_OK &&
              jelli_agent_audio_volume(DEFAULT_VOLUME);
    /* Gain masks use physical MIC numbers, not slot order: this boosts MIC1 only and
     * leaves the MIC3 reference at the codec's default. */
    ESP_LOGI(TAG, "Duplex audio %s: %u Hz, mono out, 4 TDM slots in", ok ? "ready" : "failed",
             JELLI_AGENT_AUDIO_RATE);
    return ok;
}

bool jelli_agent_audio_read(int16_t *frames, size_t frame_count)
{
    size_t bytes = frame_count * JELLI_AGENT_AUDIO_CHANNELS * sizeof(int16_t);
    return microphone && esp_codec_dev_read(microphone, frames, (int)bytes) == ESP_CODEC_DEV_OK;
}

bool jelli_agent_audio_write(const int16_t *samples, size_t count)
{
    return speaker && esp_codec_dev_write(speaker, (void *)samples,
                                          (int)(count * sizeof(int16_t))) == ESP_CODEC_DEV_OK;
}

bool jelli_agent_audio_volume(unsigned volume)
{
    return speaker && volume <= 100u &&
           esp_codec_dev_set_out_vol(speaker, (int)volume) == ESP_CODEC_DEV_OK;
}
