#include "audio_board.h"
#include "jelli/sound.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "esp_codec_dev_defaults.h"
#include "driver/i2s_std.h"

typedef struct {
    i2s_chan_handle_t tx;
    const audio_codec_gpio_if_t *gpio;
    const audio_codec_ctrl_if_t *control;
    const audio_codec_data_if_t *data;
    const audio_codec_if_t *device;
    esp_codec_dev_handle_t codec;
} AudioBoard;
static AudioBoard audio;

static void cleanup(void)
{
    if (audio.device)
        (void)audio_codec_delete_codec_if(audio.device);
    if (audio.data)
        (void)audio_codec_delete_data_if(audio.data);
    if (audio.control)
        (void)audio_codec_delete_ctrl_if(audio.control);
    if (audio.gpio)
        (void)audio_codec_delete_gpio_if(audio.gpio);
    if (audio.tx)
        (void)i2s_del_channel(audio.tx);
    audio = (AudioBoard){0};
}

static bool create_transport(void)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    if (i2s_new_channel(&channel, &audio.tx, NULL) != ESP_OK)
        return false;
    const i2s_std_config_t config = {.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(JELLI_SOUND_RATE),
                                     .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                                         I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
                                     .gpio_cfg = {.mclk = BSP_I2S_MCLK,
                                                  .bclk = BSP_I2S_SCLK,
                                                  .ws = BSP_I2S_LCLK,
                                                  .dout = BSP_I2S_DOUT,
                                                  .din = GPIO_NUM_NC}};
    if (i2s_channel_init_std_mode(audio.tx, &config) != ESP_OK)
        return false;
    /* Codec open owns channel enabling; never pre-enable an unused channel. */
    audio_codec_i2s_cfg_t data = {.port = CONFIG_BSP_I2S_NUM, .tx_handle = audio.tx};
    audio.data = audio_codec_new_i2s_data(&data);
    audio_codec_i2c_cfg_t control = {
        .port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bsp_i2c_get_handle()};
    audio.control = audio_codec_new_i2c_ctrl(&control);
    audio.gpio = audio_codec_new_gpio();
    return audio.data && audio.control && audio.gpio;
}

esp_codec_dev_handle_t jelli_audio_board_init(void)
{
    if (audio.codec)
        return audio.codec;
    if (bsp_i2c_init() != ESP_OK || !create_transport()) {
        cleanup();
        return NULL;
    }
    /* Preserve BSP 3.0.1's ES8311 DAC, clock, amplifier and gain settings. */
    es8311_codec_cfg_t config = {.ctrl_if = audio.control,
                                 .gpio_if = audio.gpio,
                                 .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
                                 .pa_pin = BSP_POWER_AMP_IO,
                                 .use_mclk = true,
                                 .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3}};
    audio.device = es8311_codec_new(&config);
    if (audio.device) {
        esp_codec_dev_cfg_t codec = {
            .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = audio.device, .data_if = audio.data};
        audio.codec = esp_codec_dev_new(&codec);
    }
    if (!audio.codec)
        cleanup();
    return audio.codec;
}
