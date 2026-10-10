#ifndef JELLI_AGENT_INPUT_H
#define JELLI_AGENT_INPUT_H

#include <stdbool.h>

/* The agent's touch screen and side button (ADR-016). Start from the main task after
 * audio (it shares the I2C bus, which the BSP sets up once); then only the UI task
 * samples, every tick. */
bool jelli_agent_input_start(void);

/* One touch sample: pressed, and where in surface pixels. False when there is no
 * touch controller or it could not be read (then *pressed is false). */
bool jelli_agent_input_touch(bool *pressed, int *x, int *y);

/* True once for each press of the programmable side button (BOOT, GPIO0), debounced
 * over two samples. Always false on a board without that pin in its wrapper. */
bool jelli_agent_input_button(void);

#endif
