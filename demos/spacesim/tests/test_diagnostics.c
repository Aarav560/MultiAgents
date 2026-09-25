/* test_diagnostics.c - unit tests for diagnostics module. */
#include "diagnostics.h"
#include "test.h"
#include <math.h>

static void test_kinetic_energy_single_body(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    /* Single body: mass = 2, velocity = (3, 4, 0), speed = 5 */
    body b = body_make("test", BODY_PARTICLE, 2.0, 0.0, vec3_make(0, 0, 0), vec3_make(3, 4, 0));
    world_add(&w, &b);

    diag d;
    diagnostics_compute(&w, &d);

    /* K = 0.5 * 2 * (3^2 + 4^2) = 0.5 * 2 * 25 = 25 */
    CHECK_NEAR(d.kinetic, 25.0, 1e-10);
    CHECK_NEAR(d.potential, 0.0, 1e-10);
    CHECK_NEAR(d.total, 25.0, 1e-10);

    world_free(&w);
}

static void test_kinetic_energy_multiple_bodies(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    /* Three bodies with known velocities:
       body 0: mass = 1, vel = (1, 0, 0), K = 0.5
       body 1: mass = 2, vel = (2, 0, 0), K = 4
       body 2: mass = 3, vel = (0, 3, 0), K = 13.5 */
    body b0 = body_make("b0", BODY_PARTICLE, 1.0, 0.0, vec3_make(0, 0, 0), vec3_make(1, 0, 0));
    body b1 = body_make("b1", BODY_PARTICLE, 2.0, 0.0, vec3_make(1, 0, 0), vec3_make(2, 0, 0));
    body b2 = body_make("b2", BODY_PARTICLE, 3.0, 0.0, vec3_make(2, 0, 0), vec3_make(0, 3, 0));

    world_add(&w, &b0);
    world_add(&w, &b1);
    world_add(&w, &b2);

    diag d;
    diagnostics_compute(&w, &d);

    /* Total K = 0.5 + 4 + 13.5 = 18 */
    CHECK_NEAR(d.kinetic, 18.0, 1e-10);

    world_free(&w);
}

static void test_potential_equilateral_triangle(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;
    w.softening = 0.0;

    /* Equilateral triangle with unit side length and unit masses.
       Vertices at: (0, 0, 0), (1, 0, 0), (0.5, sqrt(3)/2, 0)
       Distance between each pair = 1.0
       U = -G * 1 * 1 / 1 - G * 1 * 1 / 1 - G * 1 * 1 / 1 = -3 * G = -3 */

    double sqrt3_2 = sqrt(3.0) / 2.0;

    body b0 = body_make("b0", BODY_PARTICLE, 1.0, 0.0, vec3_make(0, 0, 0), vec3_make(0, 0, 0));
    body b1 = body_make("b1", BODY_PARTICLE, 1.0, 0.0, vec3_make(1, 0, 0), vec3_make(0, 0, 0));
    body b2 = body_make("b2", BODY_PARTICLE, 1.0, 0.0, vec3_make(0.5, sqrt3_2, 0), vec3_make(0, 0, 0));

    world_add(&w, &b0);
    world_add(&w, &b1);
    world_add(&w, &b2);

    diag d;
    diagnostics_compute(&w, &d);

    /* U should be -3 */
    CHECK_NEAR(d.potential, -3.0, 1e-10);
    CHECK_NEAR(d.kinetic, 0.0, 1e-10);
    CHECK_NEAR(d.total, -3.0, 1e-10);

    world_free(&w);
}

static void test_angular_momentum_circular_orbit(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    /* Circular orbit: body at (r, 0, 0) with velocity (0, v, 0)
       r × v = (r, 0, 0) × (0, v, 0) = (0, 0, r*v)
       L = m * (0, 0, r*v) = (0, 0, m*r*v) */

    double r = 2.0;
    double v = 3.0;
    double m = 5.0;

    body b = body_make("sat", BODY_PARTICLE, m, 0.0, vec3_make(r, 0, 0), vec3_make(0, v, 0));
    world_add(&w, &b);

    diag d;
    diagnostics_compute(&w, &d);

    /* Angular momentum should be (0, 0, m*r*v) = (0, 0, 30) */
    CHECK_NEAR(d.angular_momentum.x, 0.0, 1e-10);
    CHECK_NEAR(d.angular_momentum.y, 0.0, 1e-10);
    CHECK_NEAR(d.angular_momentum.z, m * r * v, 1e-10);

    world_free(&w);
}

static void test_angular_momentum_multiple_bodies(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    /* Two bodies with known angular momentum:
       body 0: pos = (1, 0, 0), vel = (0, 1, 0), mass = 2
               L = 2 * (1, 0, 0) × (0, 1, 0) = 2 * (0, 0, 1) = (0, 0, 2)
       body 1: pos = (0, 2, 0), vel = (2, 0, 0), mass = 3
               L = 3 * (0, 2, 0) × (2, 0, 0) = 3 * (0, 0, -4) = (0, 0, -12) */

    body b0 = body_make("b0", BODY_PARTICLE, 2.0, 0.0, vec3_make(1, 0, 0), vec3_make(0, 1, 0));
    body b1 = body_make("b1", BODY_PARTICLE, 3.0, 0.0, vec3_make(0, 2, 0), vec3_make(2, 0, 0));

    world_add(&w, &b0);
    world_add(&w, &b1);

    diag d;
    diagnostics_compute(&w, &d);

    /* Total L = (0, 0, 2) + (0, 0, -12) = (0, 0, -10) */
    CHECK_NEAR(d.angular_momentum.x, 0.0, 1e-10);
    CHECK_NEAR(d.angular_momentum.y, 0.0, 1e-10);
    CHECK_NEAR(d.angular_momentum.z, -10.0, 1e-10);

    world_free(&w);
}

static void test_linear_momentum(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    /* Two bodies:
       body 0: mass = 2, vel = (1, 2, 3)
       body 1: mass = 3, vel = (4, 5, 6) */

    body b0 = body_make("b0", BODY_PARTICLE, 2.0, 0.0, vec3_make(0, 0, 0), vec3_make(1, 2, 3));
    body b1 = body_make("b1", BODY_PARTICLE, 3.0, 0.0, vec3_make(0, 0, 0), vec3_make(4, 5, 6));

    world_add(&w, &b0);
    world_add(&w, &b1);

    diag d;
    diagnostics_compute(&w, &d);

    /* p = 2*(1,2,3) + 3*(4,5,6) = (2,4,6) + (12,15,18) = (14,19,24) */
    CHECK_NEAR(d.momentum.x, 14.0, 1e-10);
    CHECK_NEAR(d.momentum.y, 19.0, 1e-10);
    CHECK_NEAR(d.momentum.z, 24.0, 1e-10);

    world_free(&w);
}

static void test_center_of_mass(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    /* Two bodies:
       body 0: mass = 1, pos = (0, 0, 0)
       body 1: mass = 3, pos = (4, 0, 0)
       COM = (1*0 + 3*4) / (1+3) = (3, 0, 0) */

    body b0 = body_make("b0", BODY_PARTICLE, 1.0, 0.0, vec3_make(0, 0, 0), vec3_make(0, 0, 0));
    body b1 = body_make("b1", BODY_PARTICLE, 3.0, 0.0, vec3_make(4, 0, 0), vec3_make(0, 0, 0));

    world_add(&w, &b0);
    world_add(&w, &b1);

    diag d;
    diagnostics_compute(&w, &d);

    CHECK_NEAR(d.com.x, 3.0, 1e-10);
    CHECK_NEAR(d.com.y, 0.0, 1e-10);
    CHECK_NEAR(d.com.z, 0.0, 1e-10);

    world_free(&w);
}

static void test_virial_ratio_two_body_circular(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;
    w.softening = 0.0;

    /* Two-body circular orbit in COM frame (virial theorem: 2K + U = 0, so 2K/|U| = 1).
       Central mass M = 1 at origin, satellite mass m = 0.01 at distance r = 1 with circular velocity v_circ = sqrt(GM/r) = 1.
       In COM frame:
         Central: pos = -(m/(M+m)) * r * e_x ≈ -0.01 at (−0.01, 0, 0)
         Satellite: pos = (M/(M+m)) * r * e_x ≈ 0.99 at (0.99, 0, 0)
         Separation still r = 1.
       For circular orbit, K = 0.5 * mu * v_circ^2 / r = 0.5 * GM / r
       U = −G * M * m / r = −0.01
       K_total = K_central + K_satellite ≈ 0.5 * 0.01 * v_central^2 + 0.5 * 0.01 * v_satellite^2

       Actually, let's use a simpler approach:
       Place equal masses at (−a, 0, 0) and (a, 0, 0), each with velocity (0, v, 0).
       Then U = −G * m1 * m2 / (2a)
       For circular orbit: v_circ = sqrt(G * (m1+m2) / (2a)) = sqrt(2*G*m / (2a)) = sqrt(G*m/a)
       K = 0.5 * m * v^2 + 0.5 * m * v^2 = m * v^2 = m * G*m/a = G*m^2/a (with m_total = 2m)
       U = −G * m * m / (2a) = −G*m^2 / (2a)
       2K = 2 * G*m^2/a
       2K/|U| = (2 * G*m^2/a) / (G*m^2 / (2a)) = 2 / 0.5 = 4... hmm, that's not right.

       Let me recalculate. For a circular binary with equal masses m at separation 2a:
       Each orbits the COM at radius a with the same speed v.
       The gravitational force is F = G*m^2 / (2a)^2.
       The centripetal force is F = m * v^2 / a.
       So: G*m^2 / (4a^2) = m * v^2 / a → v^2 = G*m / (4a)
       K = 2 * 0.5 * m * v^2 = m * v^2 = G*m^2 / (4a)
       U = −G*m^2 / (2a)
       2K/|U| = (G*m^2 / (2a)) / (G*m^2 / (2a)) = 1. ✓
    */

    double a = 1.0;   /* separation from COM */
    double m = 1.0;   /* mass of each body */
    double v_circ = sqrt(1.0 * m / (4.0 * a));  /* G=1, so just sqrt(m/(4a)) */

    body b0 = body_make("m1", BODY_PARTICLE, m, 0.0, vec3_make(-a, 0, 0), vec3_make(0, v_circ, 0));
    body b1 = body_make("m2", BODY_PARTICLE, m, 0.0, vec3_make(a, 0, 0), vec3_make(0, v_circ, 0));

    world_add(&w, &b0);
    world_add(&w, &b1);

    diag d;
    diagnostics_compute(&w, &d);

    /* Virial ratio should be ≈ 1 */
    CHECK_NEAR(d.virial_ratio, 1.0, 0.01);

    world_free(&w);
}

static void test_rel_drift_zero_error(void) {
    double drift = diagnostics_rel_drift(100.0, 100.0);
    CHECK_NEAR(drift, 0.0, 1e-14);
}

static void test_rel_drift_nonzero_error(void) {
    /* |error - initial| / |initial| */
    double drift = diagnostics_rel_drift(100.0, 101.0);
    CHECK_NEAR(drift, 0.01, 1e-14);
}

static void test_rel_drift_small_denominator(void) {
    /* When denominator is < 1e-300, use 1e-300 */
    double drift = diagnostics_rel_drift(1e-350, 1e-349);
    CHECK_NEAR(drift, 9.0, 1e-10);
}

static void test_potential_with_softening(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;
    w.softening = 1.0;

    /* Two bodies at unit distance: U = −G*m1*m2 / sqrt(r^2 + eps^2) = −1 / sqrt(2) */
    body b0 = body_make("b0", BODY_PARTICLE, 1.0, 0.0, vec3_make(0, 0, 0), vec3_make(0, 0, 0));
    body b1 = body_make("b1", BODY_PARTICLE, 1.0, 0.0, vec3_make(1, 0, 0), vec3_make(0, 0, 0));

    world_add(&w, &b0);
    world_add(&w, &b1);

    diag d;
    diagnostics_compute(&w, &d);

    double expected_potential = -1.0 / sqrt(2.0);
    CHECK_NEAR(d.potential, expected_potential, 1e-10);

    world_free(&w);
}

static void test_dead_bodies_ignored(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    w.G = 1.0;

    body b0 = body_make("b0", BODY_PARTICLE, 2.0, 0.0, vec3_make(0, 0, 0), vec3_make(1, 0, 0));
    body b1 = body_make("b1", BODY_PARTICLE, 3.0, 0.0, vec3_make(1, 0, 0), vec3_make(0, 0, 0));

    world_add(&w, &b0);
    int i1 = world_add(&w, &b1);

    /* Kill one body */
    w.bodies[i1].alive = 0;

    diag d;
    diagnostics_compute(&w, &d);

    /* Only b0 contributes: K = 0.5 * 2 * 1 = 1 */
    CHECK_NEAR(d.kinetic, 1.0, 1e-10);
    CHECK_NEAR(d.potential, 0.0, 1e-10);

    world_free(&w);
}

int main(void) {
    RUN(test_kinetic_energy_single_body);
    RUN(test_kinetic_energy_multiple_bodies);
    RUN(test_potential_equilateral_triangle);
    RUN(test_angular_momentum_circular_orbit);
    RUN(test_angular_momentum_multiple_bodies);
    RUN(test_linear_momentum);
    RUN(test_center_of_mass);
    RUN(test_virial_ratio_two_body_circular);
    RUN(test_rel_drift_zero_error);
    RUN(test_rel_drift_nonzero_error);
    RUN(test_rel_drift_small_denominator);
    RUN(test_potential_with_softening);
    RUN(test_dead_bodies_ignored);

    return TEST_SUMMARY();
}
