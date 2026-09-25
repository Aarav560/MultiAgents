/* diagnostics.h - energy, momentum, and virial diagnostics for the simulation. */
#ifndef ORBIT_DIAGNOSTICS_H
#define ORBIT_DIAGNOSTICS_H

#include "world.h"

typedef struct {
    double kinetic;        /* sum of 0.5 * m * v^2 over all alive bodies */
    double potential;      /* -sum_{i<j} G m_i m_j / sqrt(r^2 + eps^2) */
    double total;          /* kinetic + potential */
    vec3 momentum;         /* sum of m * v over all alive bodies */
    vec3 angular_momentum; /* sum of m * (r × v) over all alive bodies */
    vec3 com;              /* center of mass of alive bodies */
    double virial_ratio;   /* 2 * kinetic / |potential| */
} diag;

/* Computes all diagnostic quantities for the current world state.
   Uses its own O(n^2) loop for potential (same formula as gravity_potential).
   All energies and momenta are in the world's units and coordinate system (not COM-frame). */
void diagnostics_compute(const world *w, diag *out);

/* Relative drift: |e - e0| / max(|e0|, 1e-300).
   Used to detect energy conservation: a well-integrated orbit has rel_drift near machine epsilon. */
double diagnostics_rel_drift(double e0, double e);

#endif
