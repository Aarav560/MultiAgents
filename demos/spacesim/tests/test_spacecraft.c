#include "spacecraft.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define MU_EARTH 3.986004418e14

/* Static two-body direct-sum acceleration toward a fixed central mass at the origin,
   used only for the end-to-end integration test below. */
static void central_accel(world *w, double mu) {
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) { w->bodies[i].acc = vec3_zero(); continue; }
        double r = vec3_len(w->bodies[i].pos);
        double f = -mu / (r * r * r);
        w->bodies[i].acc = vec3_scale(w->bodies[i].pos, f);
    }
}

/* Leapfrog (kick-drift-kick, velocity Verlet) step against the static central force. */
static void central_leapfrog_step(world *w, double dt, double mu) {
    central_accel(w, mu);
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) w->bodies[i].vel = vec3_madd(w->bodies[i].vel, w->bodies[i].acc, 0.5 * dt);
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) w->bodies[i].pos = vec3_madd(w->bodies[i].pos, w->bodies[i].vel, dt);
    central_accel(w, mu);
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) w->bodies[i].vel = vec3_madd(w->bodies[i].vel, w->bodies[i].acc, 0.5 * dt);
    w->t += dt;
    w->step++;
}

static void test_hohmann_leo_geo(void) {
    double r1 = 6678000.0;   /* LEO, m */
    double r2 = 42164000.0;  /* GEO, m */
    hohmann h;
    CHECK(hohmann_plan(MU_EARTH, r1, r2, &h) == 0);
    CHECK_REL(h.dv1, 2426.0, 0.005);
    CHECK_REL(h.dv2, 1467.0, 0.005);
    CHECK_REL(h.tof, 5.26 * 3600.0, 0.005);
    CHECK_REL(h.a_transfer, 0.5 * (r1 + r2), 1e-12);

    /* Bad input is rejected. */
    CHECK(hohmann_plan(0.0, r1, r2, &h) == -1);
    CHECK(hohmann_plan(MU_EARTH, -1.0, r2, &h) == -1);
    CHECK(hohmann_plan(MU_EARTH, r1, 0.0, &h) == -1);
}

static void test_plan_stays_sorted(void) {
    flight_plan p;
    flight_plan_init(&p);
    CHECK(flight_plan_add(&p, 5.0, 1, vec3_make(1, 0, 0)) == 0);
    CHECK(flight_plan_add(&p, 1.0, 2, vec3_make(2, 0, 0)) == 0);
    CHECK(flight_plan_add(&p, 3.0, 3, vec3_make(3, 0, 0)) == 1);
    CHECK(flight_plan_add(&p, 10.0, 4, vec3_make(4, 0, 0)) == 3);
    CHECK(flight_plan_add(&p, 3.0, 5, vec3_make(5, 0, 0)) == 2); /* ties: insertion order preserved */

    CHECK(p.count == 5);
    for (int i = 1; i < p.count; i++) CHECK(p.burns[i - 1].t <= p.burns[i].t);
    CHECK(p.burns[0].body_id == 2);
    CHECK(p.burns[1].body_id == 3);
    CHECK(p.burns[2].body_id == 5);
    CHECK(p.burns[3].body_id == 1);
    CHECK(p.burns[4].body_id == 4);

    flight_plan_free(&p);
    CHECK(p.burns == NULL);
    CHECK(p.count == 0);
}

static void test_apply_window_and_id_targeting(void) {
    world w;
    CHECK(world_init(&w, 4) == 0);

    body central = body_make("earth", BODY_PLANET, 5.972e24, 6.371e6, vec3_zero(), vec3_zero());
    body ship = body_make("ship", BODY_SPACECRAFT, 1000.0, 1.0, vec3_make(7000000, 0, 0), vec3_make(0, 7500, 0));
    int central_idx = world_add(&w, &central);
    int ship_idx = world_add(&w, &ship);
    CHECK(central_idx == 0);
    CHECK(ship_idx == 1);
    int ship_id = w.bodies[ship_idx].id;

    flight_plan p;
    flight_plan_init(&p);
    flight_plan_add(&p, 10.0, ship_id, vec3_make(100, 0, 0));
    flight_plan_add(&p, 20.0, ship_id, vec3_make(0, 100, 0));
    flight_plan_add(&p, 30.0, ship_id, vec3_make(0, 0, 100));

    /* Window that misses everything. */
    CHECK(flight_plan_apply(&p, &w, 0.0, 5.0) == 0);
    /* Window [10, 20) catches only the first burn (t1 exclusive). */
    CHECK(flight_plan_apply(&p, &w, 10.0, 20.0) == 1);
    CHECK_NEAR(w.bodies[ship_idx].vel.x, 100.0, 1e-9);
    CHECK(p.burns[0].done == 1);
    CHECK(p.burns[1].done == 0);

    /* Re-applying the same window does nothing (already done, fires once). */
    CHECK(flight_plan_apply(&p, &w, 10.0, 20.0) == 0);

    /* Now compact the world (no-op here since everything is alive, but must still
       resolve by id, not by index) and remove the central body to shift indices,
       then apply the rest through the id lookup. */
    w.bodies[0].alive = 0;
    world_compact(&w);
    CHECK(w.count == 1);
    CHECK(w.bodies[0].id == ship_id);

    CHECK(flight_plan_apply(&p, &w, 20.0, 40.0) == 2);
    CHECK_NEAR(w.bodies[0].vel.x, 100.0, 1e-9);
    CHECK_NEAR(w.bodies[0].vel.y, 100.0 + 7500.0, 1e-9);
    CHECK_NEAR(w.bodies[0].vel.z, 100.0, 1e-9);
    CHECK(p.burns[1].done == 1);
    CHECK(p.burns[2].done == 1);

    flight_plan_free(&p);
    world_free(&w);
}

static void test_prograde_circular(void) {
    world w;
    CHECK(world_init(&w, 4) == 0);
    body central = body_make("star", BODY_STAR, 1.0e30, 1.0, vec3_zero(), vec3_zero());
    body ship = body_make("ship", BODY_SPACECRAFT, 1.0, 1.0, vec3_make(1000.0, 0, 0), vec3_make(0, 42.0, 0));
    int ci = world_add(&w, &central);
    int si = world_add(&w, &ship);

    vec3 pg = spacecraft_prograde(&w, si, ci);
    CHECK_NEAR(pg.x, 0.0, 1e-12);
    CHECK_NEAR(pg.y, 1.0, 1e-12);
    CHECK_NEAR(pg.z, 0.0, 1e-12);
    CHECK_NEAR(vec3_len(pg), 1.0, 1e-12);

    /* Relative to a central body that is itself moving. */
    w.bodies[ci].vel = vec3_make(0, 42.0, 0);
    w.bodies[si].vel = vec3_make(5.0, 42.0, 0);
    pg = spacecraft_prograde(&w, si, ci);
    CHECK_NEAR(pg.x, 1.0, 1e-12);
    CHECK_NEAR(pg.y, 0.0, 1e-12);

    world_free(&w);
}

static void test_delta_v_budget(void) {
    flight_plan p;
    flight_plan_init(&p);
    CHECK_NEAR(spacecraft_delta_v_budget(&p), 0.0, 1e-12);
    flight_plan_add(&p, 0.0, 1, vec3_make(3.0, 4.0, 0.0));   /* len 5 */
    flight_plan_add(&p, 1.0, 1, vec3_make(0.0, 0.0, 12.0));  /* len 12, +5 = 13 via 3-4-5/5-12-13 */
    flight_plan_add(&p, 2.0, 1, vec3_zero());
    CHECK_NEAR(spacecraft_delta_v_budget(&p), 17.0, 1e-9);
    flight_plan_free(&p);
}

/* End-to-end: integrate a ship on a static central force in LEO, apply the two
   Hohmann burns, and check it ends up near the target radius. */
static void test_end_to_end_hohmann_transfer(void) {
    double mu = MU_EARTH;
    double r1 = 6678000.0;
    double r2 = 42164000.0;

    hohmann h;
    CHECK(hohmann_plan(mu, r1, r2, &h) == 0);

    world w;
    CHECK(world_init(&w, 2) == 0);
    double v_circ = sqrt(mu / r1);
    body ship = body_make("ship", BODY_SPACECRAFT, 1000.0, 1.0, vec3_make(r1, 0, 0), vec3_make(0, v_circ, 0));
    int si = world_add(&w, &ship);
    int ship_id = w.bodies[si].id;

    flight_plan p;
    flight_plan_init(&p);
    /* First burn now, second burn one transfer time-of-flight later, both prograde
       in this planar circular setup. */
    flight_plan_add(&p, 0.0, ship_id, vec3_make(0, h.dv1, 0));
    flight_plan_add(&p, h.tof, ship_id, vec3_make(0, h.dv2, 0));

    double dt = 5.0;
    double t_end = h.tof + 3600.0; /* run a bit past the arrival burn */
    double t = 0.0;
    /* Apply the t=0 burn before the first integration step. */
    flight_plan_apply(&p, &w, -1.0, 0.0 + 1e-9);
    while (t < t_end) {
        double next_t = t + dt;
        central_leapfrog_step(&w, dt, mu);
        flight_plan_apply(&p, &w, t, next_t);
        t = next_t;
    }

    double final_r = vec3_len(w.bodies[0].pos);
    CHECK_REL(final_r, r2, 0.01);

    flight_plan_free(&p);
    world_free(&w);
}

int main(void) {
    RUN(test_hohmann_leo_geo);
    RUN(test_plan_stays_sorted);
    RUN(test_apply_window_and_id_targeting);
    RUN(test_prograde_circular);
    RUN(test_delta_v_budget);
    RUN(test_end_to_end_hohmann_transfer);
    return TEST_SUMMARY();
}
