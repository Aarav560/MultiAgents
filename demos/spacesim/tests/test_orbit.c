/* test_orbit.c - Keplerian two-body mechanics. */
#include "orbit.h"
#include "rng.h"
#include "test.h"

#include <math.h>

#define MU_SUN 1.32712440018e20
#define MU_EARTH 3.986004418e14
#define AU 1.495978707e11
#define PI 3.14159265358979323846

static double vec_rel_err(vec3 a, vec3 b) { return vec3_dist(a, b) / vec3_len(b); }

static void test_earth_period(void) {
    double days = orbit_period(AU, MU_SUN) / 86400.0;
    CHECK_REL(days, 365.25, 1e-3);
    CHECK(orbit_period(0.0, MU_SUN) == 0.0);
    CHECK(orbit_period(-AU, MU_SUN) == 0.0);
}

static void test_kepler_solver(void) {
    const double es[] = {0.0, 0.1, 0.5, 0.9, 0.99};
    for (int k = 0; k < 5; k++) {
        for (int j = 0; j < 50; j++) {
            double M = -3.0 * PI + 6.0 * PI * j / 49.0;   /* includes values outside [-pi, pi] */
            double E = orbit_kepler_E(M, es[k]);
            CHECK_NEAR(E - es[k] * sin(E), M, 1e-13);
        }
    }
    CHECK(isnan(orbit_kepler_E(1.0, 1.5)));
}

static void test_true_from_mean(void) {
    /* Circular: nu = M. Periapsis and apoapsis are fixed points for every e. */
    CHECK_NEAR(orbit_true_from_mean(1.234, 0.0), 1.234, 1e-13);
    CHECK_NEAR(orbit_true_from_mean(0.0, 0.7), 0.0, 1e-13);
    CHECK_NEAR(fabs(orbit_true_from_mean(PI, 0.7)), PI, 1e-12);
    /* Series nu ~ M + 2e sin M for small e. */
    double e = 1e-4, M = 0.8;
    CHECK_NEAR(orbit_true_from_mean(M, e), M + 2.0 * e * sin(M), 1e-7);
}

static void test_round_trip(void) {
    rng g;
    rng_seed(&g, 42);
    for (int n = 0; n < 200; n++) {
        orbit_elements el = {
            rng_range(&g, 7e6, 5e11), rng_range(&g, 0.001, 0.95), rng_range(&g, 0.01, 3.1),
            rng_range(&g, 0.0, 2.0 * PI), rng_range(&g, 0.0, 2.0 * PI), rng_range(&g, 0.0, 2.0 * PI)};
        double mu = n % 2 ? MU_SUN : MU_EARTH;
        vec3 r, v, r2, v2;
        orbit_elements back;
        CHECK(orbit_to_state(&el, mu, &r, &v) == 0);
        CHECK(orbit_from_state(r, v, mu, &back) == 0);
        CHECK_REL(back.a, el.a, 1e-8);
        CHECK_NEAR(back.e, el.e, 1e-8);
        CHECK_NEAR(back.i, el.i, 1e-8);
        CHECK(orbit_to_state(&back, mu, &r2, &v2) == 0);
        CHECK(vec_rel_err(r2, r) < 1e-8);
        CHECK(vec_rel_err(v2, v) < 1e-8);
        CHECK_NEAR(vec3_len(v), orbit_vis_viva(vec3_len(r), el.a, mu), 1e-9 * vec3_len(v));
    }
}

static void test_circular_equatorial(void) {
    double r0 = 7e6, vc = sqrt(MU_EARTH / r0);
    vec3 r = vec3_make(0.0, r0, 0.0), v = vec3_make(-vc, 0.0, 0.0);
    orbit_elements el;
    CHECK(orbit_from_state(r, v, MU_EARTH, &el) == 0);
    CHECK(!isnan(el.a) && !isnan(el.e) && !isnan(el.i) && !isnan(el.raan) && !isnan(el.argp) && !isnan(el.nu));
    CHECK_REL(el.a, r0, 1e-12);
    CHECK_NEAR(el.e, 0.0, 1e-12);
    CHECK_NEAR(el.i, 0.0, 1e-12);
    CHECK(el.raan == 0.0 && el.argp == 0.0);
    CHECK_NEAR(el.nu, PI / 2.0, 1e-12);   /* undefined angles folded into nu */
    vec3 r2, v2;
    CHECK(orbit_to_state(&el, MU_EARTH, &r2, &v2) == 0);
    CHECK(vec_rel_err(r2, r) < 1e-10 && vec_rel_err(v2, v) < 1e-10);

    /* Retrograde equatorial, eccentric: argp is measured from the x axis. */
    vec3 rr = vec3_make(r0, 0.0, 0.0), vr = vec3_make(0.0, -1.1 * vc, 0.0);
    CHECK(orbit_from_state(rr, vr, MU_EARTH, &el) == 0);
    CHECK_NEAR(el.i, PI, 1e-12);
    CHECK(orbit_to_state(&el, MU_EARTH, &r2, &v2) == 0);
    CHECK(vec_rel_err(r2, rr) < 1e-10 && vec_rel_err(v2, vr) < 1e-10);

    /* Inclined circular: argp = 0, nu = argument of latitude. */
    vec3 ri = vec3_make(r0, 0.0, 0.0), vi = vec3_make(0.0, vc * cos(0.5), vc * sin(0.5));
    CHECK(orbit_from_state(ri, vi, MU_EARTH, &el) == 0);
    CHECK_NEAR(el.i, 0.5, 1e-12);
    CHECK(!isnan(el.argp) && !isnan(el.nu));
    CHECK(orbit_to_state(&el, MU_EARTH, &r2, &v2) == 0);
    CHECK(vec_rel_err(r2, ri) < 1e-9 && vec_rel_err(v2, vi) < 1e-9);
}

static void test_vis_viva(void) {
    double r0 = 7e6;
    CHECK_REL(orbit_vis_viva(r0, r0, MU_EARTH), sqrt(MU_EARTH / r0), 1e-14);
    CHECK_REL(orbit_vis_viva(r0, 1e300, MU_EARTH), sqrt(2.0 * MU_EARTH / r0), 1e-12);   /* escape */
    CHECK(orbit_vis_viva(3.0 * r0, r0, MU_EARTH) == 0.0);                            /* beyond apoapsis */
    CHECK(orbit_vis_viva(0.0, r0, MU_EARTH) == 0.0);
}

static void test_hyperbolic(void) {
    double r0 = 7e6, vesc = sqrt(2.0 * MU_EARTH / r0);
    vec3 r = vec3_make(r0, 0.0, 0.0), v = vec3_make(0.0, 1.3 * vesc, 0.2 * vesc);
    orbit_elements el;
    CHECK(orbit_from_state(r, v, MU_EARTH, &el) == 0);
    CHECK(el.e > 1.0);
    CHECK(el.a < 0.0);
    CHECK_NEAR(el.nu, 0.0, 1e-12);   /* launched at periapsis */
    CHECK_REL(vec3_len(v), orbit_vis_viva(r0, el.a, MU_EARTH), 1e-12);
    vec3 r2, v2;
    CHECK(orbit_to_state(&el, MU_EARTH, &r2, &v2) == 0);
    CHECK(vec_rel_err(r2, r) < 1e-10 && vec_rel_err(v2, v) < 1e-10);
    /* A true anomaly beyond the asymptote is unreachable. */
    el.nu = PI;
    CHECK(orbit_to_state(&el, MU_EARTH, &r2, &v2) == -1);
}

static void test_degenerate(void) {
    orbit_elements el;
    CHECK(orbit_from_state(vec3_zero(), vec3_make(1.0, 0.0, 0.0), MU_EARTH, &el) == -1);
    CHECK(orbit_from_state(vec3_make(7e6, 0.0, 0.0), vec3_make(5e3, 0.0, 0.0), MU_EARTH, &el) == -1);   /* radial */
    CHECK(orbit_from_state(vec3_make(7e6, 0.0, 0.0), vec3_make(0.0, 7e3, 0.0), 0.0, &el) == -1);
}

int main(void) {
    RUN(test_earth_period);
    RUN(test_kepler_solver);
    RUN(test_true_from_mean);
    RUN(test_round_trip);
    RUN(test_circular_equatorial);
    RUN(test_vis_viva);
    RUN(test_hyperbolic);
    RUN(test_degenerate);
    return TEST_SUMMARY();
}
