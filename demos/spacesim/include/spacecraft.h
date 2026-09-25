/* spacecraft.h - flight plans, impulsive maneuvers and Hohmann transfers. */
#ifndef ORBIT_SPACECRAFT_H
#define ORBIT_SPACECRAFT_H

#include "world.h"

/* One impulsive delta-v burn. body_id matches body.id (stable across world_compact). */
typedef struct {
    double t;      /* simulated time at which the burn fires, s */
    vec3 dv;       /* delta-v to add to the body's velocity, m/s */
    int body_id;
    int done;      /* 1 once applied */
} burn;

typedef struct {
    burn *burns;
    int count, capacity;
} flight_plan;

/* Zeroes p; no burns, no allocation yet. */
void flight_plan_init(flight_plan *p);
void flight_plan_free(flight_plan *p);

/* Inserts a burn, keeping p->burns sorted by t (stable for equal t). Returns the new
   index, or -1 on allocation failure. */
int flight_plan_add(flight_plan *p, double t, int body_id, vec3 dv);

/* Applies every not-done burn with t0 <= t < t1 to the body with matching id (by
   world_find_id-equivalent lookup over w->bodies), marks it done, and returns how
   many were applied. A burn whose body cannot be found is still marked done. */
int flight_plan_apply(flight_plan *p, world *w, double t0, double t1);

/* Unit vector along the velocity of bodies[ship_index] relative to bodies[central_index]. */
vec3 spacecraft_prograde(const world *w, int ship_index, int central_index);

typedef struct {
    double dv1;        /* m/s, burn at r1 */
    double dv2;         /* m/s, burn at r2 */
    double tof;          /* s, time of flight */
    double a_transfer;   /* m, semi-major axis of the transfer ellipse */
} hohmann;

/* Classic two-impulse Hohmann transfer between circular orbits of radius r1 and r2
   (either direction). Returns 0, or -1 if mu, r1 or r2 is not positive. */
int hohmann_plan(double mu, double r1, double r2, hohmann *out);

/* Sum of |dv| over every burn in the plan. */
double spacecraft_delta_v_budget(const flight_plan *p);

#endif
