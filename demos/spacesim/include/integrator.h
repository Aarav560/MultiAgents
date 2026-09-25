/* integrator.h - time steppers advancing a world by one step using an accel_fn. */
#ifndef ORBIT_INTEGRATOR_H
#define ORBIT_INTEGRATOR_H

#include "gravity.h"

typedef enum { INT_EULER, INT_SYMPLECTIC_EULER, INT_LEAPFROG, INT_RK4, INT_YOSHIDA4 } integrator_kind;

/* One step of size dt; calls accel(w, ctx) as needed; w->t += dt, w->step++.
   Returns 0, or -1 on bad input or allocation failure. Dead bodies are untouched. */
int integrator_step(world *w, integrator_kind k, double dt, accel_fn accel, void *ctx);
/* "euler" "symplectic" "leapfrog" "rk4" "yoshida" (NULL for an unknown kind). */
const char *integrator_name(integrator_kind k);
/* 0 ok, -1 unknown name. */
int integrator_parse(const char *name, integrator_kind *out);
/* 1 1 2 4 4 (0 for an unknown kind). */
int integrator_order(integrator_kind k);

#endif
