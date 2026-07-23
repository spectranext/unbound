#include <string.h>
#include "particles.h"
#include "client_graphics.h"
#include "soundfx.h"

void init_particles()
{
    memset(particles, 0, sizeof(particles));
}

static union {
    struct {
        uint8_t x;
        uint8_t y;
    };
    uint16_t xy;
} a, b;

static void render_particle(struct particle_t* particle) __z88dk_fastcall
{
    a.x = particle->x >> PARTICLES_PRECISION;
    a.y = particle->y >> PARTICLES_PRECISION;
    b.x = a.x + (particle->dx >> PARTICLES_PRECISION);
    b.y = a.y + (particle->dy >> PARTICLES_PRECISION);

    render_line(a.xy, b.xy);

    particle->rendered_a = a.xy;
    particle->rendered_b = b.xy;
}

static void hide_particle(struct particle_t* particle) __z88dk_fastcall
{
    render_line(particle->rendered_a, particle->rendered_b);
}

extern void add_particle(uint8_t x, uint8_t y, int8_t dx, int8_t dy, uint8_t ttl, uint8_t sound) __z88dk_callee
{
    static struct particle_t* particle;
    static uint8_t i;
    particle = particles;

    for (i = 0; i < MAX_PARTICLES; i++, particle++)
    {
        if (particle->flags & PARTICLE_LIVE)
            continue;

        particle->flags = PARTICLE_LIVE;
        particle->x = x << PARTICLES_PRECISION;
        particle->y = y << PARTICLES_PRECISION;
        particle->dx = dx;
        particle->dy = dy;
        particle->ttl = ttl;

        if (sound != 0xFF)
        {
            soundfx(sound);
        }

        break;
    }
}

void render_particles()
{
    struct particle_t* particle = particles;

    for (uint8_t i = 0; i < MAX_PARTICLES; i++, particle++)
    {
        if ((particle->flags & PARTICLE_LIVE) == 0)
            continue;

        if (particle->flags & PARTICLE_RENDERED)
        {
            // hide it
            hide_particle(particle);
            particle->flags &= ~(PARTICLE_RENDERED);
        }

        int16_t cmp = particle->dx * 2;

        if (particle->dx > 0)
        {
            if (particle->x >= (256 << PARTICLES_PRECISION) - cmp)
            {
                goto kill;
            }
        }
        else
        {
            if (particle->x <= -cmp)
            {
                goto kill;
            }
        }

        cmp = particle->dy * 2;

        if (particle->dy > 0)
        {
            if (particle->y >= (192 << PARTICLES_PRECISION) - cmp)
            {
                goto kill;
            }
        }
        else
        {
            if (particle->y <= -cmp)
            {
                goto kill;
            }
        }

        particle->x += particle->dx;

        particle->y += particle->dy;
        particle->ttl--;

        if ((particle->ttl % PARTICLES_GRAVITY) == 0)
        {
            particle->dy++;
        }

        if (particle->ttl)
        {
            render_particle(particle);
            particle->flags |= PARTICLE_RENDERED;
        }
        else
        {
kill:
            if (particle->flags & PARTICLE_RENDERED)
            {
                hide_particle(particle);
            }

            // kill it
            particle->flags = 0;
        }
    }
}

void clear_particles()
{
    struct particle_t *particle = particles;

    for (uint8_t i = 0; i < MAX_PARTICLES; i++, particle++)
    {
        if ((particle->flags & PARTICLE_LIVE) == 0)
            continue;

        particle->flags &= ~(PARTICLE_RENDERED);
    }
}

void hide_particles()
{
    struct particle_t* particle = particles;

    for (uint8_t i = 0; i < MAX_PARTICLES; i++, particle++)
    {
        if ((particle->flags & PARTICLE_LIVE) == 0)
            continue;

        if (particle->flags & PARTICLE_RENDERED)
        {
            // hide it
            hide_particle(particle);

            particle->flags |= PARTICLE_HIDDEN;
            particle->flags &= ~(PARTICLE_RENDERED);
        }
    }
}

void show_particles()
{
    struct particle_t* particle = particles;

    for (uint8_t i = 0; i < MAX_PARTICLES; i++, particle++)
    {
        if ((particle->flags & PARTICLE_LIVE) == 0)
            continue;

        if (particle->flags & PARTICLE_HIDDEN)
        {
            // show it
            render_particle(particle);

            particle->flags |= PARTICLE_RENDERED;
            particle->flags &= ~(PARTICLE_HIDDEN);
        }
    }
}