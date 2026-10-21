#ifndef JELLI_BUBBLE_H
#define JELLI_BUBBLE_H
#include <stdbool.h>
#include <stdint.h>
#define JELLI_BUBBLE_WIDTH 6u
#define JELLI_BUBBLE_HEIGHT 10u
#define JELLI_BUBBLE_CELLS (JELLI_BUBBLE_WIDTH * JELLI_BUBBLE_HEIGHT)
#define JELLI_BUBBLE_INPUTS 4u
#define JELLI_BUBBLE_LEVELS 3u
#define JELLI_BUBBLE_PAIRS 60u
#define JELLI_BUBBLE_FALL_TICKS 8u
#define JELLI_BUBBLE_RELAXED_TICKS 16u
/* Cells: 0 empty, 1..3 medicine, 5..7 stationary viruses (color = value & 3). */
typedef enum {
    JELLI_BUBBLE_RUNNING,
    JELLI_BUBBLE_WON,
    JELLI_BUBBLE_TOP,
    JELLI_BUBBLE_EMPTY
} JelliBubbleStatus;
typedef enum { JELLI_BUBBLE_NORMAL, JELLI_BUBBLE_RELAXED, JELLI_BUBBLE_MANUAL } JelliBubbleAssist;
typedef struct {
    uint8_t cells[JELLI_BUBBLE_CELLS];
    uint16_t score, level_score;
    uint8_t level, cursor, x, y, vertical, color, status, ticks, assist;
    uint8_t input[JELLI_BUBBLE_INPUTS], input_count;
} JelliBubble;
/* Init/retry preserve only selected assistance; retry retains completed levels/score. */
void jelli_bubble_init(JelliBubble *lab, unsigned assist);
void jelli_bubble_retry(JelliBubble *lab);
bool jelli_bubble_same(const JelliBubble *a, const JelliBubble *b);
bool jelli_bubble_valid(const JelliBubble *lab);
bool jelli_bubble_move(JelliBubble *lab, int dx, bool rotate);
void jelli_bubble_hard_drop(JelliBubble *lab);
void jelli_bubble_drop(JelliBubble *lab);
void jelli_bubble_tick(JelliBubble *lab);
unsigned jelli_bubble_viruses(const JelliBubble *lab);
#endif
