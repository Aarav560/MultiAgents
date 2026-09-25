/* orbit.h - Keplerian two-body mechanics: classical elements, Kepler's equation, vis-viva. */
#ifndef ORBIT_ORBIT_H
#define ORBIT_ORBIT_H

#include "vec3.h"

/* a (m, negative for hyperbolic), e, i, raan, argp, nu (rad; nu = true anomaly). */
typedef struct {
    double a, e, i, raan, argp, nu;
} orbit_elements;

/* Elliptic or hyperbolic; -1 on degenerate input (r = 0, h = 0, parabolic, mu <= 0). */
int orbit_from_state(vec3 r, vec3 v, double mu, orbit_elements *out);
/* -1 when the elements describe no reachable point (p <= 0, 1 + e cos nu <= 0, mu <= 0). */
int orbit_to_state(const orbit_elements *el, double mu, vec3 *r, vec3 *v);
/* 2 pi sqrt(a^3/mu); returns 0 when a <= 0. */
double orbit_period(double a, double mu);
/* Solves M = E - e sin E for 0 <= e < 1 (Newton, 1e-14); NaN for e outside [0, 1). */
double orbit_kepler_E(double M, double e);
double orbit_true_from_mean(double M, double e);
/* Speed sqrt(mu (2/r - 1/a)); 0 when r <= 0 or the radicand is negative. */
double orbit_vis_viva(double r, double a, double mu);

#endif
