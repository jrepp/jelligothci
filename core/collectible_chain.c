#include "jelli/collectible.h"
#include <stddef.h>

static bool occupied(const JelliChain *chain, unsigned cell, unsigned length)
{
    for (unsigned i = 0u; i < length; ++i)
        if (chain->body[i] == cell)
            return true;
    return false;
}

static void place_pickup(JelliChain *chain)
{
    /* Deterministic bounded placement, no retries or dependence on host RNG. */
    for (unsigned i = 1u; i <= JELLI_CHAIN_GRID * JELLI_CHAIN_GRID; ++i) {
        unsigned cell = (chain->body[0] + i * 13u) % (JELLI_CHAIN_GRID * JELLI_CHAIN_GRID);
        if (!occupied(chain, cell, chain->length)) {
            chain->pickup = (uint8_t)cell;
            return;
        }
    }
}

static void place_star(JelliChain *chain)
{
    const unsigned turns[3] = {0u, 3u, 1u};
    for (unsigned i = 0u; i < 3u; ++i) {
        unsigned direction = (chain->direction + turns[i]) % 4u;
        int x = (int)(chain->body[0] % JELLI_CHAIN_GRID);
        int y = (int)(chain->body[0] / JELLI_CHAIN_GRID);
        x += direction == JELLI_CHAIN_RIGHT ? 1 : direction == JELLI_CHAIN_LEFT ? -1 : 0;
        y += direction == JELLI_CHAIN_DOWN ? 1 : direction == JELLI_CHAIN_UP ? -1 : 0;
        if (x >= 0 && y >= 0 && x < (int)JELLI_CHAIN_GRID && y < (int)JELLI_CHAIN_GRID) {
            unsigned cell = (unsigned)y * JELLI_CHAIN_GRID + (unsigned)x;
            if (!occupied(chain, cell, chain->length)) {
                chain->pickup = (uint8_t)cell;
                return;
            }
        }
    }
    /* At odd length7 head/tail share grid parity, so the tail cannot be an
     * adjacent escape. No free legal neighbor means genuinely trapped. */
    chain->status = JELLI_CHAIN_LOST;
}

void jelli_chain_init(JelliChain *chain, bool relaxed)
{
    if (!chain)
        return;
    *chain = (JelliChain){.body = {27u, 26u, 25u},
                          .length = 3u,
                          .direction = JELLI_CHAIN_RIGHT,
                          .pending_direction = JELLI_CHAIN_RIGHT,
                          .relaxed = relaxed};
    place_pickup(chain);
}

static bool chain_header_valid(const JelliChain *chain)
{
    if (!chain || chain->length < 3u || chain->length > JELLI_CHAIN_LENGTH ||
        chain->direction > JELLI_CHAIN_LEFT || chain->pending_direction > JELLI_CHAIN_LEFT ||
        chain->buffered_direction > 4u ||
        (chain->buffered_direction &&
         (!chain->turned ||
          chain->buffered_direction - 1u == (chain->pending_direction + 2u) % 4u)) ||
        chain->status > JELLI_CHAIN_LOST || chain->pickup >= JELLI_CHAIN_GRID * JELLI_CHAIN_GRID ||
        (!chain->turned && chain->pending_direction != chain->direction) ||
        (chain->turned && chain->pending_direction == (chain->direction + 2u) % 4u) ||
        chain->step_ticks >=
            (chain->relaxed ? JELLI_CHAIN_RELAXED_TICKS : JELLI_CHAIN_STEP_TICKS) ||
        (chain->status == JELLI_CHAIN_WON && chain->length != JELLI_CHAIN_LENGTH))
        return false;
    return true;
}

bool jelli_chain_valid(const JelliChain *chain)
{
    if (!chain_header_valid(chain))
        return false;
    for (unsigned i = 0u; i < chain->length; ++i) {
        unsigned cell = chain->body[i];
        if (cell >= JELLI_CHAIN_GRID * JELLI_CHAIN_GRID || occupied(chain, cell, i))
            return false;
        if (i) {
            unsigned previous = chain->body[i - 1u];
            unsigned x = cell % JELLI_CHAIN_GRID, y = cell / JELLI_CHAIN_GRID;
            unsigned px = previous % JELLI_CHAIN_GRID, py = previous / JELLI_CHAIN_GRID;
            unsigned distance = (x > px ? x - px : px - x) + (y > py ? y - py : py - y);
            if (distance != 1u)
                return false;
        }
    }
    return chain->status == JELLI_CHAIN_WON || !occupied(chain, chain->pickup, chain->length);
}

bool jelli_chain_turn(JelliChain *chain, unsigned direction)
{
    if (!jelli_chain_valid(chain) || chain->status != JELLI_CHAIN_RUNNING ||
        direction > JELLI_CHAIN_LEFT)
        return false;
    unsigned last = chain->buffered_direction ? chain->buffered_direction - 1u
                    : chain->turned           ? chain->pending_direction
                                              : chain->direction;
    if (direction == last)
        return true;
    if (direction == (last + 2u) % 4u || chain->buffered_direction)
        return false;
    if (chain->turned)
        chain->buffered_direction = (uint8_t)(direction + 1u);
    else {
        chain->pending_direction = (uint8_t)direction;
        chain->turned = true;
    }
    return true;
}

static void move_chain(JelliChain *chain)
{
    int x = (int)(chain->body[0] % JELLI_CHAIN_GRID);
    int y = (int)(chain->body[0] / JELLI_CHAIN_GRID);
    chain->direction = chain->pending_direction;
    chain->turned = chain->buffered_direction != 0u;
    if (chain->turned)
        chain->pending_direction = (uint8_t)(chain->buffered_direction - 1u);
    chain->buffered_direction = 0u;
    x += chain->direction == JELLI_CHAIN_RIGHT ? 1 : chain->direction == JELLI_CHAIN_LEFT ? -1 : 0;
    y += chain->direction == JELLI_CHAIN_DOWN ? 1 : chain->direction == JELLI_CHAIN_UP ? -1 : 0;
    if (x < 0 || y < 0 || x >= (int)JELLI_CHAIN_GRID || y >= (int)JELLI_CHAIN_GRID) {
        chain->status = JELLI_CHAIN_LOST;
        return;
    }
    unsigned cell = (unsigned)y * JELLI_CHAIN_GRID + (unsigned)x;
    bool eat = cell == chain->pickup;
    unsigned length = chain->length;
    if (occupied(chain, cell, length - (eat ? 0u : 1u))) {
        chain->status = JELLI_CHAIN_LOST;
        return;
    }
    if (eat && length < JELLI_CHAIN_LENGTH)
        ++length;
    for (unsigned i = length - 1u; i > 0u; --i)
        chain->body[i] = chain->body[i - 1u];
    chain->body[0] = (uint8_t)cell;
    if (eat && chain->length == JELLI_CHAIN_LENGTH)
        chain->status = JELLI_CHAIN_WON;
    chain->length = (uint8_t)length;
    if (eat && chain->status != JELLI_CHAIN_WON) {
        place_pickup(chain);
        if (chain->length == JELLI_CHAIN_LENGTH)
            place_star(chain);
    }
}

void jelli_chain_tick(JelliChain *chain)
{
    if (!jelli_chain_valid(chain) || chain->status != JELLI_CHAIN_RUNNING)
        return;
    ++chain->step_ticks;
    if (chain->step_ticks >=
        (chain->relaxed ? JELLI_CHAIN_RELAXED_TICKS : JELLI_CHAIN_STEP_TICKS)) {
        chain->step_ticks = 0u;
        move_chain(chain);
    }
}
