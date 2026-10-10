#ifndef JELLI_PARTICLES_H
#define JELLI_PARTICLES_H
#include "jelli/engine.h"

#define JELLI_PARTICLE_CAPACITY 24u
enum { JELLI_PARTICLE_BUBBLE = 32u, JELLI_PARTICLE_RISE = 16u };
/* Q4 positions and Q4 pixels per 20 ms velocities. Zero life means free.
 * style packs a two-bit palette index, size bit, and cross/square bit. */
typedef struct {
    int16_t x, y;
    int8_t vx, vy;
    uint8_t life, style;
} JelliParticle;

typedef struct {
    JelliParticle items[JELLI_PARTICLE_CAPACITY];
    JelliRect previous;
    uint32_t random_state;
    uint8_t remainder_ms, next;
    bool changed;
} JelliParticles;

void jelli_particles_burst(JelliParticles *particles, int x, int y, bool accepted);
void jelli_particles_advance(JelliParticles *particles, uint64_t elapsed_ms);
void jelli_particles_advance_scaled(JelliParticles *particles, uint64_t elapsed_ms,
                                    unsigned step_ms);
void jelli_particles_burst_tuned(JelliParticles *particles, int x, int y, bool accepted,
                                 unsigned amount, unsigned spread);
unsigned jelli_particles_count(const JelliParticles *particles);
JelliRect jelli_particles_bounds(const JelliParticles *particles);
#endif
