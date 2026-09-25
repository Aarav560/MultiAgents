/* integrator.c - Euler, symplectic Euler, leapfrog (KDK), RK4 and Yoshida 4th order. */
#include "integrator.h"

#include <stdlib.h>
#include <string.h>

static const char *const names[] = {"euler", "symplectic", "leapfrog", "rk4", "yoshida"};
static const int orders[] = {1, 1, 2, 4, 4};
#define KIND_COUNT 5

static void drift(world *w, double h) {
    for (int i = 0; i < w->count; i++) {
        body *b = &w->bodies[i];
        if (b->alive) b->pos = vec3_madd(b->pos, b->vel, h);
    }
}

static void kick(world *w, double h) {
    for (int i = 0; i < w->count; i++) {
        body *b = &w->bodies[i];
        if (b->alive) b->vel = vec3_madd(b->vel, b->acc, h);
    }
}

/* Explicit Euler: x' = x + h v, v' = v + h a(x). */
static void step_euler(world *w, double h, accel_fn accel, void *ctx) {
    accel(w, ctx);
    for (int i = 0; i < w->count; i++) {
        body *b = &w->bodies[i];
        if (!b->alive) continue;
        b->pos = vec3_madd(b->pos, b->vel, h);
        b->vel = vec3_madd(b->vel, b->acc, h);
    }
}

/* Symplectic Euler (kick then drift). */
static void step_symplectic(world *w, double h, accel_fn accel, void *ctx) {
    accel(w, ctx);
    kick(w, h);
    drift(w, h);
}

/* Velocity Verlet, kick-drift-kick. */
static void step_leapfrog(world *w, double h, accel_fn accel, void *ctx) {
    accel(w, ctx);
    kick(w, 0.5 * h);
    drift(w, h);
    accel(w, ctx);
    kick(w, 0.5 * h);
}

/* Yoshida (1990) 4th-order composition of three drift-kick-drift leapfrogs:
   w1 = 1/(2 - 2^(1/3)), w0 = -2^(1/3) w1; c = {w1/2, (w0+w1)/2, (w0+w1)/2, w1/2}, d = {w1, w0, w1}. */
static void step_yoshida(world *w, double h, accel_fn accel, void *ctx) {
    double cbrt2 = cbrt(2.0);
    double w1 = 1.0 / (2.0 - cbrt2);
    double w0 = -cbrt2 * w1;
    double c[4] = {0.5 * w1, 0.5 * (w0 + w1), 0.5 * (w0 + w1), 0.5 * w1};
    double d[3] = {w1, w0, w1};
    for (int s = 0; s < 3; s++) {
        drift(w, c[s] * h);
        accel(w, ctx);
        kick(w, d[s] * h);
    }
    drift(w, c[3] * h);
}

/* Classical RK4 on (x, v): stage state = base + f * k_prev, result = base + h/6 (k1 + 2k2 + 2k3 + k4). */
static int step_rk4(world *w, double h, accel_fn accel, void *ctx) {
    int n = w->count;
    if (n == 0) return 0;
    vec3 *buf = malloc(sizeof(vec3) * 4 * (size_t)n);
    if (!buf) return -1;
    vec3 *x0 = buf, *v0 = buf + n, *sx = buf + 2 * n, *sv = buf + 3 * n;
    for (int i = 0; i < n; i++) {
        x0[i] = w->bodies[i].pos;
        v0[i] = w->bodies[i].vel;
        sx[i] = vec3_zero();
        sv[i] = vec3_zero();
    }
    static const double stage_f[3] = {0.5, 0.5, 1.0};
    static const double weight[4] = {1.0, 2.0, 2.0, 1.0};
    for (int s = 0; s < 4; s++) {
        accel(w, ctx);
        for (int i = 0; i < n; i++) {
            body *b = &w->bodies[i];
            if (!b->alive) continue;
            vec3 kx = b->vel, kv = b->acc;
            sx[i] = vec3_madd(sx[i], kx, weight[s]);
            sv[i] = vec3_madd(sv[i], kv, weight[s]);
            if (s < 3) {
                b->pos = vec3_madd(x0[i], kx, stage_f[s] * h);
                b->vel = vec3_madd(v0[i], kv, stage_f[s] * h);
            }
        }
    }
    for (int i = 0; i < n; i++) {
        body *b = &w->bodies[i];
        if (!b->alive) continue;
        b->pos = vec3_madd(x0[i], sx[i], h / 6.0);
        b->vel = vec3_madd(v0[i], sv[i], h / 6.0);
    }
    free(buf);
    return 0;
}

int integrator_step(world *w, integrator_kind k, double dt, accel_fn accel, void *ctx) {
    if (!w || !accel || !(dt == dt)) return -1;
    switch (k) {
    case INT_EULER: step_euler(w, dt, accel, ctx); break;
    case INT_SYMPLECTIC_EULER: step_symplectic(w, dt, accel, ctx); break;
    case INT_LEAPFROG: step_leapfrog(w, dt, accel, ctx); break;
    case INT_RK4:
        if (step_rk4(w, dt, accel, ctx) != 0) return -1;
        break;
    case INT_YOSHIDA4: step_yoshida(w, dt, accel, ctx); break;
    default: return -1;
    }
    w->t += dt;
    w->step++;
    return 0;
}

const char *integrator_name(integrator_kind k) {
    if ((int)k < 0 || (int)k >= KIND_COUNT) return NULL;
    return names[k];
}

int integrator_parse(const char *name, integrator_kind *out) {
    if (!name || !out) return -1;
    for (int i = 0; i < KIND_COUNT; i++) {
        if (strcmp(name, names[i]) == 0) {
            *out = (integrator_kind)i;
            return 0;
        }
    }
    return -1;
}

int integrator_order(integrator_kind k) {
    if ((int)k < 0 || (int)k >= KIND_COUNT) return 0;
    return orders[k];
}
