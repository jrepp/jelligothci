#ifndef JELLI_AUDIO_BOARD_H
#define JELLI_AUDIO_BOARD_H
#include "esp_codec_dev.h"

/* Startup-only playback path. Owns one TX channel and codec interfaces for
 * app lifetime; no unused RX channel. Returns NULL after cleanup on failure. */
esp_codec_dev_handle_t jelli_audio_board_init(void);
#endif
