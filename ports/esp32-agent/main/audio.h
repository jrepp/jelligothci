#ifndef JELLI_AGENT_AUDIO_H
#define JELLI_AGENT_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Audio for the voice agent (RFC-003): the ES8311 speaker in standard I2S and the
 * ES7210 microphones in TDM on one I2S port, so both run at the same rate. 24 kHz
 * matches Chatter's output. Capture delivers all four TDM slots, interleaved in the
 * ES7210's order MIC1, MIC3, MIC2, MIC4; MIC3 carries the speaker's echo reference
 * (memo-025). */
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
