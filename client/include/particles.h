#ifndef PARTICLES_H
#define PARTICLES_H

#include <stdint.h>

#define MAX_PARTICLES (8)
#define PARTICLES_GRAVITY (2)
#define PARTICLES_PRECISION 2

#define PARTICLE_LIVE 0x01
#define PARTICLE_RENDERED 0x02
#define PARTICLE_HIDDEN 0x04

struct particle_t
{
    uint8_t flags;
    uint16_t x;
    uint16_t y;
    int8_t dx;
    int8_t dy;
    uint8_t ttl;

    uint16_t rendered_a;
    uint16_t rendered_b;
};

extern struct particle_t particles[MAX_PARTICLES];

extern void init_particles();
extern void add_particle(uint8_t x, uint8_t y, int8_t dx, int8_t dy, uint8_t ttl, uint8_t sound) __z88dk_callee;
extern void render_particles();
extern void clear_particles();
extern void hide_particles();
extern void show_particles();

#endif