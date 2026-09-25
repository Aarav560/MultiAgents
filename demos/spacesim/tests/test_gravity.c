/* test_gravity.c - direct-summation gravity: magnitudes, directions, momentum balance,
 * softening at r=0, dead bodies, and potential energy, in both SI and G=1 units. */
#include "test.h"

#include "gravity.h"
#include "world.h"

#include <math.h>

static world make_world(double G, double softening, int cap) {
    world w;
    CHECK(world_init(&w, cap) == 0);
    w.G = G;
    w.softening = softening;
    return w;
}

static void test_two_body_magnitude_and_direction(void) {
    world w = make_world(G_SI, 0.0, 4);

    double m1 = 5.972e24; /* Earth-like */
    double m2 = 7.348e22; /* Moon-like */
    double r = 3.844e8;

    body b1 = body_make("A", BODY_PLANET, m1, 1.0, vec3_make(0, 0, 0), vec3_zero());
    body b2 = body_make("B", BODY_MOON, m2, 1.0, vec3_make(r, 0, 0), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);

    gravity_direct(&w, NULL);

    double expected1 = w.G * m2 / (r * r);
    double expected2 = w.G * m1 / (r * r);

    /* b1 pulled toward b2: +x. b2 pulled toward b1: -x. */
    CHECK_REL(w.bodies[0].acc.x, expected1, 1e-9);
    CHECK_NEAR(w.bodies[0].acc.y, 0.0, 1e-30);
    CHECK_NEAR(w.bodies[0].acc.z, 0.0, 1e-30);

    CHECK_REL(-w.bodies[1].acc.x, expected2, 1e-9);
    CHECK_NEAR(w.bodies[1].acc.y, 0.0, 1e-30);
    CHECK_NEAR(w.bodies[1].acc.z, 0.0, 1e-30);

    CHECK_REL(vec3_len(w.bodies[0].acc), expected1, 1e-9);
    CHECK_REL(vec3_len(w.bodies[1].acc), expected2, 1e-9);

    world_free(&w);
}

static void test_three_body_momentum_balance(void) {
    world w = make_world(1.0, 0.0, 4);

    body b1 = body_make("A", BODY_PARTICLE, 1.0, 0.01, vec3_make(1, 0, 0), vec3_zero());
    body b2 = body_make("B", BODY_PARTICLE, 2.0, 0.01, vec3_make(-1, 1, 0.5), vec3_zero());
    body b3 = body_make("C", BODY_PARTICLE, 3.0, 0.01, vec3_make(0.5, -1.5, 1), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);
    world_add(&w, &b3);

    gravity_direct(&w, NULL);

    vec3 sum = vec3_zero();
    for (int i = 0; i < w.count; i++) {
        sum = vec3_madd(sum, w.bodies[i].acc, w.bodies[i].mass);
    }

    double scale = 0.0;
    for (int i = 0; i < w.count; i++) {
        scale += w.bodies[i].mass * vec3_len(w.bodies[i].acc);
    }

    CHECK(vec3_len(sum) <= 1e-12 * scale);

    world_free(&w);
}

static void test_softening_caps_acceleration_at_zero(void) {
    /* Exactly coincident bodies: separation vector is zero, so by symmetry the
       force is exactly zero (not undefined/NaN) regardless of softening. */
    world w = make_world(1.0, 0.1, 4);

    body b1 = body_make("A", BODY_PARTICLE, 1.0, 0.01, vec3_zero(), vec3_zero());
    body b2 = body_make("B", BODY_PARTICLE, 1.0, 0.01, vec3_zero(), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);

    gravity_direct(&w, NULL);

    CHECK(isfinite(w.bodies[0].acc.x));
    CHECK(isfinite(w.bodies[0].acc.y));
    CHECK(isfinite(w.bodies[0].acc.z));
    CHECK(isfinite(w.bodies[1].acc.x));
    CHECK_NEAR(vec3_len(w.bodies[0].acc), 0.0, 1e-300);
    CHECK_NEAR(vec3_len(w.bodies[1].acc), 0.0, 1e-300);

    double u = gravity_potential(&w);
    CHECK(isfinite(u));
    CHECK_REL(u, -w.G * 1.0 * 1.0 / 0.1, 1e-9);

    /* A tiny but nonzero separation is where softening actually matters: it
       must stay finite and be capped well below the unsoftened 1/r^2 value. */
    world w2 = make_world(1.0, 0.1, 4);
    body c1 = body_make("A", BODY_PARTICLE, 1.0, 0.01, vec3_make(-1e-6, 0, 0), vec3_zero());
    body c2 = body_make("B", BODY_PARTICLE, 1.0, 0.01, vec3_make(1e-6, 0, 0), vec3_zero());
    world_add(&w2, &c1);
    world_add(&w2, &c2);

    gravity_direct(&w2, NULL);

    CHECK(isfinite(w2.bodies[0].acc.x));
    CHECK(isfinite(w2.bodies[1].acc.x));
    double unsoftened = w2.G * 1.0 / (2e-6 * 2e-6); /* would diverge without softening */
    CHECK(vec3_len(w2.bodies[0].acc) < unsoftened);
    /* eps^2 dominates r^2 here, so |a| ~= G m r / eps^3 (small, not diverging). */
    double expected_near = w2.G * 1.0 * 2e-6 / (0.1 * 0.1 * 0.1);
    CHECK_REL(vec3_len(w2.bodies[0].acc), expected_near, 1e-6);

    world_free(&w2);
    world_free(&w);
}

static void test_dead_bodies_zero_acc_and_no_force(void) {
    world w = make_world(1.0, 0.0, 4);

    body b1 = body_make("A", BODY_PARTICLE, 1.0, 0.01, vec3_make(0, 0, 0), vec3_zero());
    body b2 = body_make("B", BODY_PARTICLE, 100.0, 0.01, vec3_make(1, 0, 0), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);
    w.bodies[1].alive = 0;

    gravity_direct(&w, NULL);

    CHECK_NEAR(w.bodies[1].acc.x, 0.0, 0.0);
    CHECK_NEAR(w.bodies[1].acc.y, 0.0, 0.0);
    CHECK_NEAR(w.bodies[1].acc.z, 0.0, 0.0);

    /* The dead body exerts no force on the living one either. */
    CHECK_NEAR(w.bodies[0].acc.x, 0.0, 0.0);
    CHECK_NEAR(w.bodies[0].acc.y, 0.0, 0.0);
    CHECK_NEAR(w.bodies[0].acc.z, 0.0, 0.0);

    world_free(&w);
}

static void test_potential_two_body(void) {
    {
        world w = make_world(G_SI, 0.0, 4);
        double m1 = 5.972e24, m2 = 7.348e22, r = 3.844e8;
        body b1 = body_make("A", BODY_PLANET, m1, 1.0, vec3_make(0, 0, 0), vec3_zero());
        body b2 = body_make("B", BODY_MOON, m2, 1.0, vec3_make(r, 0, 0), vec3_zero());
        world_add(&w, &b1);
        world_add(&w, &b2);

        double u = gravity_potential(&w);
        CHECK_REL(u, -w.G * m1 * m2 / r, 1e-9);
        world_free(&w);
    }
    {
        /* G = 1 (N-body units) */
        world w = make_world(1.0, 0.0, 4);
        double m1 = 2.0, m2 = 3.0, r = 5.0;
        body b1 = body_make("A", BODY_PARTICLE, m1, 0.01, vec3_make(0, 0, 0), vec3_zero());
        body b2 = body_make("B", BODY_PARTICLE, m2, 0.01, vec3_make(r, 0, 0), vec3_zero());
        world_add(&w, &b1);
        world_add(&w, &b2);

        double u = gravity_potential(&w);
        CHECK_REL(u, -m1 * m2 / r, 1e-12);
        world_free(&w);
    }
}

int main(void) {
    RUN(test_two_body_magnitude_and_direction);
    RUN(test_three_body_momentum_balance);
    RUN(test_softening_caps_acceleration_at_zero);
    RUN(test_dead_bodies_zero_acc_and_no_force);
    RUN(test_potential_two_body);
    return TEST_SUMMARY();
}
