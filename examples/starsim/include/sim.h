/* sim.h - the simulated universe: bodies, world bookkeeping, gravity, integration, collisions.
 * Units are per scenario (w->G says which); nothing here assumes SI. */
#ifndef STARSIM_SIM_H
#define STARSIM_SIM_H

#include <stdint.h>

#include "vec2.h"

#define BODY_NAME_MAX 24

typedef enum {
    KIND_STAR = 0,
    KIND_PLANET,
    KIND_MOON,
    KIND_ASTEROID,
    KIND_DUST,
    KIND_SHIP,
    KIND_BLACKHOLE,
    KIND_COUNT
} body_kind;

typedef struct {
    vec2 pos, vel, acc;
    double mass;
    double radius;          /* world units; used for collisions and as the minimum drawn size */
    uint32_t color;         /* 0x00RRGGBB */
    body_kind kind;
    int alive;              /* 0 once merged away; removed by world_compact */
    int id;                 /* unique and stable for the life of the world (assigned by world_add) */
    char name[BODY_NAME_MAX];
} body;

typedef struct {
    body *b;
    int n, cap;
    double G;               /* gravitational constant in this scenario's units */
    double soft;            /* Plummer softening length */
    double t;               /* simulated time */
    int next_id;
} world;

typedef enum { GRAV_DIRECT = 0, GRAV_BH = 1 } gravity_mode;

/* ---- world.c (already written) ---------------------------------------- */
/* Returns a body with alive = 1, id = -1, acc = 0, name copied (truncated; NULL allowed). */
body body_make(const char *name, body_kind kind, double mass, double radius, vec2 pos, vec2 vel,
               uint32_t color);
int world_init(world *w, int cap);            /* G = 1, soft = 0; 0 or -1 (OOM) */
void world_free(world *w);
void world_clear(world *w);                    /* n = 0, t = 0; keeps G/soft */
int world_add(world *w, const body *b);        /* assigns id, grows; index or -1 */
int world_compact(world *w);                   /* drops !alive, keeps order; returns removed */
int world_index_of(const world *w, int id);   /* index of alive body with that id, or -1 */
int world_nearest(const world *w, vec2 p, double max_dist); /* alive body closest to p within max_dist, or -1 */
double world_mass(const world *w);
vec2 world_com(const world *w);
vec2 world_momentum(const world *w);
void world_recenter(world *w);                 /* moves to the centre-of-mass frame */

/* ---- physics.c --------------------------------------------------------- */
/* Direct O(n^2) gravity: fills acc of every alive body (dead bodies get 0). */
void gravity_direct(world *w);
/* Dispatches to gravity_direct or gravity_bh. theta is used only for GRAV_BH. */
void gravity_compute(world *w, gravity_mode mode, double theta);
/* One leapfrog kick-drift-kick step of size dt (computes accelerations itself). w->t += dt. */
void sim_step(world *w, double dt, gravity_mode mode, double theta);
/* Kinetic + potential energy (potential O(n^2) with softening, same formula as gravity). */
double world_energy(const world *w);
/* Merges every overlapping pair (distance < r1 + r2), perfectly inelastically: mass and momentum
   conserved, position = mass-weighted mean, area conserved (r = sqrt(r1^2 + r2^2)). The heavier
   body survives (keeps id, name, kind, color). Repeats until no overlaps. Returns merges (does NOT
   compact). Must use a uniform grid when n > 64. */
int collide_merge(world *w);
/* Predicts the path of a massless test particle starting at pos/vel under the gravity of the
   current bodies (frozen in place), using leapfrog with step dt. Writes up to max_pts positions to
   out (out[0] = pos) and returns how many were written; stops early on hitting a body. */
int predict_path(const world *w, vec2 pos, vec2 vel, double dt, int max_pts, vec2 *out);

/* ---- quadtree.c -------------------------------------------------------- */
/* Barnes-Hut gravity with a 2D quadtree (pooled nodes, leaf buckets). Same result as
   gravity_direct when theta = 0. Allocation failure falls back to gravity_direct. */
void gravity_bh(world *w, double theta);

#endif
