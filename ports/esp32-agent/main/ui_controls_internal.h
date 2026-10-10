#ifndef JELLI_AGENT_UI_CONTROLS_INTERNAL_H
#define JELLI_AGENT_UI_CONTROLS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* Between the controls (ui_controls.c, the UI task's) and their console commands
 * (ui_controls_console.c, the console task's). */

/* Touches, presses, and screen changes the console asks for, as the UI task's input. */
typedef enum {
    ASK_TAP,
    ASK_DRAG,
    ASK_BUTTON,
    ASK_SLEEP,
    ASK_WAKE,
    ASK_HOLD,
    ASK_RELEASE,
    ASK_PET
} AskKind;

typedef struct {
    uint8_t kind;
    int x, y;
} Ask;

/* Any task: queues an ask for the UI task; false when the controls have not started or
 * the queue is full. Never blocks. */
bool jelli_agent_ui_controls_ask(AskKind kind, int x, int y);

/* Any task: whether the screen is dark, Settings' screen (JelliPrefsScreen), and how many
 * networks it lists; false when the controls have not started. */
bool jelli_agent_ui_controls_status(bool *dark, int *view, int *networks);

#endif
