#ifndef JELLI_AGENT_AUDIO_H
#define JELLI_AGENT_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Full-duplex audio for the phase 0 spike (RFC-003): the ES8311 speaker in standard
 * I2S and the ES7210 microphones in TDM on one I2S port, so both run at the same
 * rate. 24 kHz matches Chatter's output. Capture delivers all four TDM slots,
 * interleaved in the ES7210's order MIC1, MIC3, MIC2, MIC4, so the spike can
 * measure which one carries the speaker echo reference the board wires to MIC3. */
#define JELLI_AGENT_AUDIO_RATE 24000u
#define JELLI_AGENT_AUDIO_CHANNELS 4u

bool jelli_agent_audio_start(void);
/* Fills frames (frame_count * 4 int16) with one sample per TDM slot; blocks. */
bool jelli_agent_audio_read(int16_t *frames, size_t frame_count);
/* Plays mono samples; blocks until they are queued for DMA. */
bool jelli_agent_audio_write(const int16_t *samples, size_t count);
/* 0..100 on the pet's calibrated curve (memo-021); 20 is about unity gain. */
bool jelli_agent_audio_volume(unsigned volume);

#endif
