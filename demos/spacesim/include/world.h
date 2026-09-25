/* world.h - the simulated system: a growable array of bodies plus global constants. */
#ifndef ORBIT_WORLD_H
#define ORBIT_WORLD_H

#include "body.h"

#define G_SI 6.67430e-11 /* m^3 kg^-1 s^-2 */

typedef struct {
    body *bodies;
    int count;               /* bodies[0..count) are valid (some may have alive = 0) */
    int capacity;
    double t;                /* simulated time, s */
    double G;                /* gravitational constant in use (G_SI or 1.0) */
    double softening;        /* Plummer softening length eps: r^2 -> r^2 + eps^2 */
    unsigned long long step; /* completed steps */
    int next_id;
} world;

/* Allocates room for capacity bodies (minimum 4). G = G_SI, softening = 0.
   Returns 0, or -1 on allocation failure. */
int world_init(world *w, int capacity);
void world_free(world *w);
/* Copies b in, assigns b.id, grows the array as needed. Returns the new index or -1. */
int world_add(world *w, const body *b);
/* First alive body whose name matches exactly, or NULL. */
body *world_find(world *w, const char *name);
/* Removes bodies with alive == 0, preserving order. Returns how many were removed. */
int world_compact(world *w);
/* Number of alive bodies. */
int world_alive(const world *w);
double world_total_mass(const world *w);
/* Mass-weighted center of mass and total linear momentum of alive bodies. */
vec3 world_com(const world *w);
vec3 world_com_vel(const world *w);
vec3 world_momentum(const world *w);
/* Shifts positions and velocities into the center-of-mass frame. */
void world_recenter(world *w);
/* Largest distance of any alive body from the center of mass (0 when empty). */
double world_extent(const world *w);

#endif
