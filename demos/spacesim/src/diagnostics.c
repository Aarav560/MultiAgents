/* diagnostics.c - energy, momentum, and virial diagnostics. */
#include "diagnostics.h"
#include "world.h"
#include <math.h>

void diagnostics_compute(const world *w, diag *out) {
    if (!w || !out) return;

    out->kinetic = 0.0;
    out->potential = 0.0;
    out->momentum = vec3_zero();
    out->angular_momentum = vec3_zero();

    /* Kinetic energy and linear momentum. */
    for (int i = 0; i < w->count; i++) {
        const body *b = &w->bodies[i];
        if (!b->alive) continue;

        double v2 = vec3_len2(b->vel);
        out->kinetic += 0.5 * b->mass * v2;
        out->momentum = vec3_add(out->momentum, vec3_scale(b->vel, b->mass));
    }

    /* Potential energy and angular momentum. */
    for (int i = 0; i < w->count; i++) {
        const body *bi = &w->bodies[i];
        if (!bi->alive) continue;

        /* Angular momentum: L = sum(m * r × v) */
        vec3 ri_cross_vi = vec3_cross(bi->pos, bi->vel);
        out->angular_momentum = vec3_add(out->angular_momentum, vec3_scale(ri_cross_vi, bi->mass));

        /* Pairwise potential: U = -sum_{i<j} G m_i m_j / sqrt(r^2 + eps^2) */
        for (int j = i + 1; j < w->count; j++) {
            const body *bj = &w->bodies[j];
            if (!bj->alive) continue;

            vec3 rij = vec3_sub(bj->pos, bi->pos);
            double r2 = vec3_len2(rij);
            double r2_soft = r2 + w->softening * w->softening;
            double r_soft = sqrt(r2_soft);

            out->potential -= w->G * bi->mass * bj->mass / r_soft;
        }
    }

    out->total = out->kinetic + out->potential;

    /* Virial ratio: 2 * K / |U| */
    if (out->potential != 0.0) {
        out->virial_ratio = 2.0 * out->kinetic / fabs(out->potential);
    } else {
        out->virial_ratio = 0.0;
    }

    /* Center of mass. */
    out->com = world_com(w);
}

double diagnostics_rel_drift(double e0, double e) {
    double den = fabs(e0) > 1e-300 ? fabs(e0) : 1e-300;
    return fabs(e - e0) / den;
}
