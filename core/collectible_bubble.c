#include "jelli/bubble.h"
#include <stddef.h>
#include <string.h>

static bool fits(const JelliBubble *lab, unsigned x, unsigned y, unsigned vertical)
{
    unsigned bx = x + (vertical ? 0u : 1u), by = y + vertical;
    return bx < JELLI_BUBBLE_WIDTH && by < JELLI_BUBBLE_HEIGHT &&
           !lab->cells[y * JELLI_BUBBLE_WIDTH + x] && !lab->cells[by * JELLI_BUBBLE_WIDTH + bx];
}

static void spawn(JelliBubble *lab)
{
    lab->x = 2u;
    lab->y = 0u;
    lab->vertical = 0u;
    lab->ticks = 0u;
    lab->color = (uint8_t)(lab->cursor % 3u + 1u);
    if (lab->cursor == JELLI_BUBBLE_PAIRS)
        lab->status = JELLI_BUBBLE_EMPTY;
    else if (!fits(lab, lab->x, lab->y, lab->vertical))
        lab->status = JELLI_BUBBLE_TOP;
    else
        ++lab->cursor;
}

static void level_init(JelliBubble *lab, unsigned level, unsigned score, unsigned assist)
{
    *lab = (JelliBubble){.level = (uint8_t)level,
                         .score = (uint16_t)score,
                         .level_score = (uint16_t)score,
                         .assist = (uint8_t)assist};
    /* Three separate color columns. Each level adds a row of stationary viruses.
     * Vertical matching-color pairs solve every authored well; spare columns and
     * a repeated supply leave room to recover from misplaced pairs. */
    for (unsigned row = 0u; row <= level; ++row)
        for (unsigned color = 1u; color <= 3u; ++color)
            lab->cells[(JELLI_BUBBLE_HEIGHT - 1u - row) * JELLI_BUBBLE_WIDTH + color * 2u - 1u] =
                (uint8_t)(color | 4u);
    spawn(lab);
}

void jelli_bubble_init(JelliBubble *lab, unsigned assist)
{
    if (lab && assist <= JELLI_BUBBLE_MANUAL)
        level_init(lab, 0u, 0u, assist);
}

void jelli_bubble_retry(JelliBubble *lab)
{
    if (!jelli_bubble_valid(lab))
        return;
    if (lab->status == JELLI_BUBBLE_WON)
        jelli_bubble_init(lab, lab->assist);
    else
        level_init(lab, lab->level, lab->level_score, lab->assist);
}

unsigned jelli_bubble_viruses(const JelliBubble *lab)
{
    unsigned count = 0u;
    if (lab)
        for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; ++i)
            count += lab->cells[i] >= 5u ? 1u : 0u;
    return count;
}

static bool header_valid(const JelliBubble *lab)
{
    if (!lab || lab->level >= JELLI_BUBBLE_LEVELS || !lab->cursor ||
        lab->cursor > JELLI_BUBBLE_PAIRS || lab->x >= JELLI_BUBBLE_WIDTH ||
        lab->y >= JELLI_BUBBLE_HEIGHT || lab->vertical > 1u || !lab->color || lab->color > 3u ||
        lab->status > JELLI_BUBBLE_EMPTY || lab->assist > JELLI_BUBBLE_MANUAL ||
        lab->input_count > JELLI_BUBBLE_INPUTS || lab->ticks >= JELLI_BUBBLE_RELAXED_TICKS ||
        lab->level_score > lab->score)
        return false;
    return true;
}

bool jelli_bubble_valid(const JelliBubble *lab)
{
    if (!header_valid(lab))
        return false;
    for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; ++i)
        if (lab->cells[i] == 4u || lab->cells[i] > 7u)
            return false;
    for (unsigned i = 0u; i < lab->input_count; ++i)
        if (lab->input[i] > 3u)
            return false;
    unsigned viruses = jelli_bubble_viruses(lab);
    if (viruses > (lab->level + 1u) * 3u)
        return false;
    if (lab->status == JELLI_BUBBLE_WON)
        return !viruses && lab->level == JELLI_BUBBLE_LEVELS - 1u;
    return viruses &&
           (lab->status != JELLI_BUBBLE_RUNNING || fits(lab, lab->x, lab->y, lab->vertical));
}

bool jelli_bubble_move(JelliBubble *lab, int dx, bool rotate)
{
    if (!jelli_bubble_valid(lab) || lab->status != JELLI_BUBBLE_RUNNING || dx < -1 || dx > 1)
        return false;
    int x = (int)lab->x + dx;
    unsigned vertical = rotate ? 1u - lab->vertical : lab->vertical;
    if (x < 0 || !fits(lab, (unsigned)x, lab->y, vertical))
        return false;
    lab->x = (uint8_t)x;
    lab->vertical = (uint8_t)vertical;
    return true;
}

static void gravity(JelliBubble *lab)
{
    /* Bottom-up, each half independent. Viruses never move; side cells never support. */
    for (unsigned row = JELLI_BUBBLE_HEIGHT - 1u; row > 0u; --row)
        for (unsigned x = 0u; x < JELLI_BUBBLE_WIDTH; ++x) {
            unsigned cell = (row - 1u) * JELLI_BUBBLE_WIDTH + x;
            if (!lab->cells[cell] || lab->cells[cell] > 3u)
                continue;
            unsigned target = cell;
            for (unsigned n = 0u; n < JELLI_BUBBLE_HEIGHT - row; ++n) {
                if (lab->cells[target + JELLI_BUBBLE_WIDTH])
                    break;
                target += JELLI_BUBBLE_WIDTH;
            }
            if (target != cell) {
                lab->cells[target] = lab->cells[cell];
                lab->cells[cell] = 0u;
            }
        }
}

static unsigned mark_runs(const JelliBubble *lab, bool *marked)
{
    unsigned count = 0u;
    for (unsigned cell = 0u; cell < JELLI_BUBBLE_CELLS; ++cell) {
        unsigned color = lab->cells[cell] & 3u;
        if (!color)
            continue;
        for (unsigned axis = 0u; axis < 2u; ++axis) {
            unsigned stride = axis ? JELLI_BUBBLE_WIDTH : 1u;
            unsigned remaining = axis ? JELLI_BUBBLE_HEIGHT - cell / JELLI_BUBBLE_WIDTH
                                      : JELLI_BUBBLE_WIDTH - cell % JELLI_BUBBLE_WIDTH;
            unsigned length = 1u;
            for (; length < remaining; ++length)
                if ((lab->cells[cell + length * stride] & 3u) != color)
                    break;
            if (length >= 4u)
                for (unsigned i = 0u; i < length; ++i) {
                    unsigned index = cell + i * stride;
                    count += marked[index] ? 0u : 1u;
                    marked[index] = true;
                }
        }
    }
    return count;
}

static void resolve(JelliBubble *lab)
{
    gravity(lab);
    /* Every successful pass removes at least four cells. */
    for (unsigned cascade = 1u; cascade <= JELLI_BUBBLE_CELLS / 4u; ++cascade) {
        bool marked[JELLI_BUBBLE_CELLS] = {false};
        if (!mark_runs(lab, marked))
            break;
        unsigned points = 0u;
        for (unsigned i = 0u; i < JELLI_BUBBLE_CELLS; ++i)
            if (marked[i]) {
                points += lab->cells[i] >= 5u ? 100u : 10u;
                lab->cells[i] = 0u;
            }
        unsigned score = lab->score + points * cascade;
        lab->score = (uint16_t)(score > UINT16_MAX ? UINT16_MAX : score);
        gravity(lab);
    }
}

void jelli_bubble_drop(JelliBubble *lab)
{
    if (!jelli_bubble_valid(lab) || lab->status != JELLI_BUBBLE_RUNNING)
        return;
    lab->ticks = 0u;
    if (fits(lab, lab->x, lab->y + 1u, lab->vertical)) {
        ++lab->y;
        return;
    }
    unsigned cell = lab->y * JELLI_BUBBLE_WIDTH + lab->x;
    lab->cells[cell] = lab->color;
    lab->cells[cell + (lab->vertical ? JELLI_BUBBLE_WIDTH : 1u)] = lab->color;
    resolve(lab);
    if (!jelli_bubble_viruses(lab)) {
        if (lab->level + 1u == JELLI_BUBBLE_LEVELS)
            lab->status = JELLI_BUBBLE_WON;
        else
            level_init(lab, lab->level + 1u, lab->score, lab->assist);
        return;
    }
    for (unsigned x = 0u; x < JELLI_BUBBLE_WIDTH; ++x)
        if (lab->cells[x]) {
            lab->status = JELLI_BUBBLE_TOP;
            return;
        }
    spawn(lab);
}

void jelli_bubble_hard_drop(JelliBubble *lab)
{
    if (!jelli_bubble_valid(lab))
        return;
    unsigned cursor = lab->cursor, level = lab->level;
    for (unsigned i = 0u; i <= JELLI_BUBBLE_HEIGHT; ++i) {
        jelli_bubble_drop(lab);
        if (lab->status != JELLI_BUBBLE_RUNNING || lab->cursor != cursor || lab->level != level)
            break;
    }
}

void jelli_bubble_tick(JelliBubble *lab)
{
    if (!jelli_bubble_valid(lab) || lab->status != JELLI_BUBBLE_RUNNING ||
        lab->assist == JELLI_BUBBLE_MANUAL)
        return;
    ++lab->ticks;
    unsigned delay =
        lab->assist == JELLI_BUBBLE_RELAXED ? JELLI_BUBBLE_RELAXED_TICKS : JELLI_BUBBLE_FALL_TICKS;
    if (lab->ticks >= delay) {
        /* Keep the state valid before entering the public drop validator. */
        lab->ticks = 0u;
        jelli_bubble_drop(lab);
    }
}

bool jelli_bubble_same(const JelliBubble *a, const JelliBubble *b)
{
    return a && b && a->score == b->score && a->level_score == b->level_score &&
           a->level == b->level && a->cursor == b->cursor && a->x == b->x && a->y == b->y &&
           a->vertical == b->vertical && a->color == b->color && a->status == b->status &&
           a->ticks == b->ticks && a->assist == b->assist && a->input_count == b->input_count &&
           memcmp(a->input, b->input, a->input_count) == 0 &&
           memcmp(a->cells, b->cells, sizeof(a->cells)) == 0;
}
