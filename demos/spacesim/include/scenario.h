/* scenario.h - built-in universes: fill an empty world with bodies. */
#ifndef ORBIT_SCENARIO_H
#define ORBIT_SCENARIO_H

#include "world.h"

typedef struct {
    double dt;             /* suggested integration step, s */
    double duration;       /* suggested run length, s */
    double scale;           /* suggested render scale, metres per pixel for an 800 px frame (0 = auto) */
    char description[128]; /* short human-readable blurb */
} scenario_info;

/* Loads scenario `name` into `w` (already initialized and empty). Fills `info` if non-NULL.
   Returns 0 on success, -1 for an unknown name. Deterministic for a given seed;
   scenarios that don't use randomness ignore seed. Ends in the center-of-mass frame. */
int scenario_load(world *w, const char *name, unsigned long long seed, scenario_info *info);

/* Number of built-in scenarios. */
int scenario_count(void);

/* Name of the scenario at index [0, scenario_count()), or NULL if out of range. */
const char *scenario_name(int index);

#endif
