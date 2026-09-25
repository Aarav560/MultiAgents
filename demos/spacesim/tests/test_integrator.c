/* test_integrator.c - integrators on a test particle circling a fixed unit mass (G = 1, r = 1). */
#include "integrator.h"
#include "test.h"

#include <string.h>

#define TWO_PI 6.283185307179586

/* Fixed central mass M = 1 at the origin; the world holds only the test particle(s). */
static void central_accel(world *w, void *ctx) {
    (void)ctx;
    for (int i = 0; i < w->count; i++) {
        body *b = &w->bodies[i];
        if (!b->alive) {
            b->acc = vec3_zero();
            continue;
        }
        double r = vec3_len(b->pos);
        b->acc = vec3_scale(b->pos, -w->G / (r * r * r));
    }
}

static void setup(world *w) {
    world_init(w, 4);
    w->G = 1.0;
    body p = body_make("p", BODY_PARTICLE, 1e-10, 0.0, vec3_make(1, 0, 0), vec3_make(0, 1, 0));
    world_add(w, &p);
}

static double energy(const world *w) {
    const body *b = &w->bodies[0];
    return 0.5 * vec3_len2(b->vel) - w->G / vec3_len(b->pos);
}

/* Position error after one period (T = 2 pi) with n steps. */
static double period_error(integrator_kind k, int n) {
    world w;
    setup(&w);
    double dt = TWO_PI / n;
    for (int i = 0; i < n; i++) integrator_step(&w, k, dt, central_accel, NULL);
    double err = vec3_dist(w.bodies[0].pos, vec3_make(1, 0, 0));
    world_free(&w);
    return err;
}

static void test_one_period(void) {
    CHECK(period_error(INT_LEAPFROG, 1000) < 1e-3);
    CHECK(period_error(INT_RK4, 1000) < 1e-8);
    CHECK(period_error(INT_YOSHIDA4, 1000) < 1e-8);
    CHECK(period_error(INT_EULER, 1000) > 1e-2);
    CHECK(period_error(INT_SYMPLECTIC_EULER, 1000) < 1e-1);
}

static void check_ratio(integrator_kind k, int n, double expected) {
    double r = period_error(k, n) / period_error(k, 2 * n);
    CHECK(r > expected * 0.7 && r < expected * 1.3);
    if (!(r > expected * 0.7 && r < expected * 1.3))
        fprintf(stderr, "  %s ratio %g\n", integrator_name(k), r);
}

static void test_convergence(void) {
    check_ratio(INT_LEAPFROG, 200, 4.0);
    check_ratio(INT_RK4, 200, 16.0);
    check_ratio(INT_YOSHIDA4, 200, 16.0);
    check_ratio(INT_EULER, 2000, 2.0);
}

static double energy_drift(integrator_kind k, int orbits, int per_orbit) {
    world w;
    setup(&w);
    double e0 = energy(&w), worst = 0.0;
    double dt = TWO_PI / per_orbit;
    for (int i = 0; i < orbits * per_orbit; i++) {
        integrator_step(&w, k, dt, central_accel, NULL);
        double d = fabs(energy(&w) - e0) / fabs(e0);
        if (d > worst) worst = d;
    }
    world_free(&w);
    return worst;
}

static void test_energy(void) {
    CHECK(energy_drift(INT_LEAPFROG, 100, 1000) < 1e-4);
    CHECK(energy_drift(INT_YOSHIDA4, 100, 1000) < 1e-4);
    /* Euler: energy grows monotonically, several times more over 10 orbits than over 1. */
    double e1 = energy_drift(INT_EULER, 1, 1000), e10 = energy_drift(INT_EULER, 10, 1000);
    CHECK(e1 > 1e-3);
    CHECK(e10 > 3.0 * e1);
    world w;
    setup(&w);
    double e0 = energy(&w);
    for (int i = 0; i < 1000; i++) integrator_step(&w, INT_EULER, TWO_PI / 1000, central_accel, NULL);
    CHECK(energy(&w) > e0);
    world_free(&w);
}

static void test_time_and_dead(void) {
    for (int k = INT_EULER; k <= INT_YOSHIDA4; k++) {
        world w;
        setup(&w);
        body d = body_make("dead", BODY_PARTICLE, 1.0, 0.0, vec3_make(2, 0, 0), vec3_make(0, 0.5, 0));
        int di = world_add(&w, &d);
        w.bodies[di].alive = 0;
        for (int i = 0; i < 10; i++) CHECK(integrator_step(&w, (integrator_kind)k, 0.01, central_accel, NULL) == 0);
        CHECK_NEAR(w.t, 0.1, 1e-12);
        CHECK(w.step == 10);
        CHECK(w.bodies[di].pos.x == 2.0 && w.bodies[di].pos.y == 0.0);
        CHECK(w.bodies[di].vel.y == 0.5);
        CHECK(vec3_dist(w.bodies[0].pos, vec3_make(1, 0, 0)) > 1e-3);
        world_free(&w);
    }
    world w;
    setup(&w);
    CHECK(integrator_step(&w, (integrator_kind)99, 0.1, central_accel, NULL) == -1);
    CHECK(integrator_step(&w, INT_RK4, 0.1, NULL, NULL) == -1);
    CHECK(w.step == 0);
    world_free(&w);
}

static void test_names(void) {
    const char *expect[] = {"euler", "symplectic", "leapfrog", "rk4", "yoshida"};
    const int order[] = {1, 1, 2, 4, 4};
    for (int k = INT_EULER; k <= INT_YOSHIDA4; k++) {
        const char *n = integrator_name((integrator_kind)k);
        CHECK(n && strcmp(n, expect[k]) == 0);
        integrator_kind out = INT_EULER;
        CHECK(integrator_parse(n, &out) == 0);
        CHECK(out == (integrator_kind)k);
        CHECK(integrator_order((integrator_kind)k) == order[k]);
    }
    integrator_kind out;
    CHECK(integrator_parse("verlet", &out) == -1);
    CHECK(integrator_parse(NULL, &out) == -1);
    CHECK(integrator_name((integrator_kind)42) == NULL);
}

int main(void) {
    RUN(test_one_period);
    RUN(test_convergence);
    RUN(test_energy);
    RUN(test_time_and_dead);
    RUN(test_names);
    return TEST_SUMMARY();
}
