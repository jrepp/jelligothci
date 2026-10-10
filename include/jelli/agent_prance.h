#ifndef JELLI_AGENT_PRANCE_H
#define JELLI_AGENT_PRANCE_H

#include <stdbool.h>
#include <stdint.h>

/* The unicorn's prancing routines (RFC-003, ADR-015's agent): where it is and which
 * frame it shows at any moment, as a pure function of the time since the routine
 * began, so the board and tests replay the same motion. Frames face right; mirror
 * flips them to face left. Positions are the frame's centre in pixels from the panel's
 * centre (y grows downwards), chosen so the unicorn drawn in a JELLI_AGENT_PRANCE_FRAME_PX
 * frame stays inside the round panel (tests/test_agent_prance_art.c measures it). */

enum { JELLI_AGENT_PRANCE_FRAME_PX = 260 };

/* In the order of the agent art (IDs 13001-13008). */
typedef enum {
    JELLI_PRANCE_A,
    JELLI_PRANCE_PASS_A,
    JELLI_PRANCE_B,
    JELLI_PRANCE_PASS_B,
    JELLI_PRANCE_LEAP,
    JELLI_PRANCE_LAND,
    JELLI_PRANCE_REAR,
    JELLI_PRANCE_STAND,
    JELLI_PRANCE_FRAMES
} JelliPranceFrame;

typedef enum {
    JELLI_PRANCE_WANDER,    /* Now and then while idle: across the screen and back. */
    JELLI_PRANCE_CELEBRATE, /* When a task succeeds: rears, hops, rears again. */
    JELLI_PRANCE_FROLIC,    /* When petted (ADR-017): leaps to and fro across the screen. */
} JelliPranceRoutine;

typedef struct {
    JelliPranceFrame frame;
    int x, y;
    bool mirror;
} JelliPrancePose;

/* The pose ms milliseconds into routine; false once the routine has ended (the pose
 * is then its last one, standing at the centre). */
bool jelli_agent_prance_at(JelliPranceRoutine routine, uint32_t ms, JelliPrancePose *pose);

/* How long routine lasts, in milliseconds. */
uint32_t jelli_agent_prance_length(JelliPranceRoutine routine);

#endif
