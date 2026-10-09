#ifndef JELLI_AGENT_RENDER_BENCH_H
#define JELLI_AGENT_RENDER_BENCH_H

#include "jelli/agent_console.h"
#include <stdbool.h>
#include <stddef.h>

/* RFC-003 phase 0 render spike: "render bench MODE SECONDS" animates the unicorn at
 * 384 px (logical scale 12 of the 192 px art) for 1..10 s and reports frames per
 * second with render and present time:
 *   full   whole screen redrawn and presented each frame (worst case)
 *   bob    body moves up and down; the union of old and new bounds is redrawn
 *   patch  only a head-sized region changes (eyes, mouth): the common update
 * The unicorn stays on screen afterwards. Returns false for other verbs. */
bool jelli_agent_command_render(const JelliAgentRequest *request, char *out, size_t capacity);

#endif
