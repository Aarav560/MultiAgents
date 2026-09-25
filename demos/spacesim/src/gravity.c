/* gravity.c - O(n^2) direct-summation Newtonian gravity with Plummer softening. */
#include "gravity.h"

#include <stddef.h>

void gravity_direct(world *w, void *ctx) {
    (void)ctx;
    if (w == NULL) return;

    for (int i = 0; i < w->count; i++) {
        w->bodies[i].acc = vec3_zero();
    }

    double eps2 = w->softening * w->softening;

    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        for (int j = i + 1; j < w->count; j++) {
            if (!w->bodies[j].alive) continue;

            vec3 d = vec3_sub(w->bodies[j].pos, w->bodies[i].pos);
            double r2 = vec3_len2(d) + eps2;
            double inv_r = 1.0 / sqrt(r2);
            double inv_r3 = inv_r / r2;

            /* a_i += G m_j (r_j - r_i) / (r^2 + eps^2)^{3/2} */
            w->bodies[i].acc = vec3_madd(w->bodies[i].acc, d, w->G * w->bodies[j].mass * inv_r3);
            w->bodies[j].acc = vec3_madd(w->bodies[j].acc, d, -w->G * w->bodies[i].mass * inv_r3);
        }
    }
}

double gravity_potential(const world *w) {
    if (w == NULL) return 0.0;

    double eps2 = w->softening * w->softening;
    double u = 0.0;

    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        for (int j = i + 1; j < w->count; j++) {
            if (!w->bodies[j].alive) continue;

            double r2 = vec3_dist(w->bodies[i].pos, w->bodies[j].pos);
            r2 = r2 * r2 + eps2;
            u -= w->G * w->bodies[i].mass * w->bodies[j].mass / sqrt(r2);
        }
    }

    return u;
}
