#ifndef JELLI_AGENT_VOICE_H
#define JELLI_AGENT_VOICE_H

#include <stdbool.h>
#include <stdint.h>

/* Hold to talk (RFC-003): while the user holds, MIC1 streams to Loki Bridge as loki/1
 * mic frames of 60 ms. Capture and sending are separate tasks with a PSRAM queue
 * between them, so a slow send never overruns the microphone's DMA (memo-025); a
 * queue that fills drops frames and ptt_end counts them. */
bool jelli_agent_voice_start(void);

/* Begins an utterance: stops any reply (the board never listens while it speaks),
 * sends ptt_start, and starts capturing. False when offline, already holding, or the
 * last utterance's ptt_end has not gone yet. */
bool jelli_agent_voice_begin(void);

/* Ends it: the last frame is marked final and ptt_end follows. Utterances also end
 * by themselves after 30 seconds. */
void jelli_agent_voice_end(void);

bool jelli_agent_voice_holding(void);

#endif
