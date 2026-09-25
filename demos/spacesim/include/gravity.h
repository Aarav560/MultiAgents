/* gravity.h - direct-summation gravity and the accel_fn interface every solver implements. */
#ifndef ORBIT_GRAVITY_H
#define ORBIT_GRAVITY_H

#include "world.h"

/* Fills acc of every alive body (and zeroes acc of dead ones). ctx is solver-specific. */
typedef void (*accel_fn)(world *w, void *ctx);

/* O(n^2) pairwise Newtonian gravity with Plummer softening; ctx unused. */
void gravity_direct(world *w, void *ctx);

/* -sum_{i<j} G m_i m_j / sqrt(r^2 + eps^2) over alive bodies. */
double gravity_potential(const world *w);

#endif
