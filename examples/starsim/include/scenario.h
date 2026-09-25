/* scenario.h - built-in universes. */
#ifndef STARSIM_SCENARIO_H
#define STARSIM_SCENARIO_H

#include "sim.h"

typedef struct {
    vec2 view_center;       /* where the camera starts */
    double view_radius;     /* world distance that should fit in half the window's smaller side */
    double dt;              /* physics step */
    int steps_per_frame;    /* steps at 1x time warp (60 fps assumed) */
    gravity_mode gravity;   /* recommended solver */
    int collisions;         /* 1 = merge on contact */
    const char *time_unit;  /* label for w->t, e.g. "years" or "" for N-body units */
    double time_per_unit;   /* w->t / time_per_unit is shown with time_unit (1 if none) */
} scenario_view;

int scenario_count(void);
const char *scenario_name(int index);          /* short, e.g. "Solar System" */
const char *scenario_desc(int index);          /* one line */
/* Clears w, sets G and soft, adds bodies, recenters. Returns 0, or -1 for a bad index. */
int scenario_load(world *w, int index, unsigned seed, scenario_view *view);
/* Index of the scenario whose name matches case-insensitively (or a 1-based number), else -1. */
int scenario_find(const char *name_or_number);

#endif
