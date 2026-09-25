#include <stdlib.h>
#include <string.h>

#include "sim.h"
#include "test.h"

/* Link stand-in for quadtree.c. */
void gravity_bh(world *w, double theta) {
    (void)theta;
    gravity_direct(w);
}

static const double PI = 3.14159265358979323846;

static uint32_t rng_state = 12345u;
static double rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 8) / 16777216.0;
}

static void two_body(world *w, double m_planet, vec2 vel) {
    world_init(w, 4);
    body sun = body_make("sun", KIND_STAR, 1.0, 0.01, v2(0, 0), v2(0, 0), 0xffffff);
    body p = body_make("p", KIND_PLANET, m_planet, 0.001, v2(1, 0), vel, 0x00ff00);
    world_add(w, &sun);
    world_add(w, &p);
}

static void test_circular_orbit_period(void) {
    world w;
    two_body(&w, 1e-12, v2(0, 1));
    double T = 2 * PI, dt = T / 2000;
    for (int i = 0; i < 2000; i++) sim_step(&w, dt, GRAV_DIRECT, 0.0);
    CHECK(v2_dist(w.b[1].pos, v2(1, 0)) < 1e-3);
    CHECK_NEAR(v2_len(w.b[1].pos), 1.0, 1e-3);
    CHECK_NEAR(w.t, T, 1e-9);
    world_free(&w);
}

static void test_energy_bounded(void) {
    world w;
    two_body(&w, 1e-3, v2(0, 0.8)); /* eccentric orbit */
    double e0 = world_energy(&w), worst = 0.0;
    double a = 1.0 / (2.0 / 1.0 - 0.64 / 1.001), T = 2 * PI * sqrt(a * a * a / 1.001);
    int steps = 50 * 2000;
    double dt = T / 2000;
    for (int i = 0; i < steps; i++) {
        sim_step(&w, dt, GRAV_DIRECT, 0.0);
        double d = fabs((world_energy(&w) - e0) / e0);
        if (d > worst) worst = d;
    }
    CHECK(worst < 1e-4);
    world_free(&w);
}

static void test_momentum_conserved(void) {
    world w;
    world_init(&w, 8);
    w.soft = 0.01;
    for (int i = 0; i < 5; i++) {
        body b = body_make(NULL, KIND_PLANET, 0.5 + rnd(), 0.01, v2(rnd() * 4 - 2, rnd() * 4 - 2),
                           v2(rnd() - 0.5, rnd() - 0.5), 0);
        world_add(&w, &b);
    }
    vec2 p0 = world_momentum(&w);
    double scale = 0.0;
    for (int i = 0; i < 5; i++) scale += w.b[i].mass * v2_len(w.b[i].vel);
    for (int i = 0; i < 1000; i++) sim_step(&w, 0.001, GRAV_BH, 0.5);
    vec2 p1 = world_momentum(&w);
    CHECK(v2_dist(p0, p1) <= 1e-12 * scale);
    world_free(&w);
}

static void test_merge_pair(void) {
    world w;
    world_init(&w, 4);
    body a = body_make("light", KIND_ASTEROID, 1.0, 0.3, v2(0, 0), v2(1, 2), 0x111111);
    body b = body_make("heavy", KIND_PLANET, 3.0, 0.4, v2(0.5, 0), v2(-1, 0), 0x222222);
    world_add(&w, &a);
    int hid = w.b[world_add(&w, &b)].id;
    double m0 = world_mass(&w);
    vec2 p0 = world_momentum(&w), c0 = world_com(&w);
    CHECK(collide_merge(&w) == 1);
    CHECK(!w.b[0].alive);
    CHECK(w.b[1].alive);
    CHECK(w.b[1].id == hid);
    CHECK(strcmp(w.b[1].name, "heavy") == 0);
    CHECK(w.b[1].kind == KIND_PLANET && w.b[1].color == 0x222222);
    CHECK_NEAR(world_mass(&w), m0, 1e-15);
    CHECK_NEAR(world_momentum(&w).x, p0.x, 1e-15);
    CHECK_NEAR(world_momentum(&w).y, p0.y, 1e-15);
    CHECK_NEAR(w.b[1].pos.x, c0.x, 1e-15);
    CHECK_NEAR(w.b[1].radius, 0.5, 1e-15);
    CHECK(world_compact(&w) == 1);
    CHECK(w.n == 1);
    world_free(&w);
}

static void test_no_merge_when_apart(void) {
    world w;
    world_init(&w, 4);
    body a = body_make(NULL, KIND_ASTEROID, 1.0, 0.3, v2(0, 0), v2(0, 0), 0);
    body b = body_make(NULL, KIND_ASTEROID, 1.0, 0.2, v2(0.5, 0), v2(0, 0), 0);
    world_add(&w, &a);
    world_add(&w, &b);
    CHECK(collide_merge(&w) == 0); /* touching exactly is not overlapping */
    world_free(&w);
}

static void test_chain_merge(void) {
    world w;
    world_init(&w, 4);
    /* A and B overlap; the merged body (r = sqrt(2)*0.5) then reaches C. */
    body a = body_make(NULL, KIND_ASTEROID, 1.0, 0.5, v2(0, 0), v2(0, 0), 0);
    body b = body_make(NULL, KIND_ASTEROID, 1.0, 0.5, v2(0.9, 0), v2(0, 0), 0);
    body c = body_make(NULL, KIND_ASTEROID, 1.0, 0.1, v2(0.45, 0.75), v2(0, 0), 0);
    world_add(&w, &a);
    world_add(&w, &b);
    world_add(&w, &c);
    CHECK(collide_merge(&w) == 2);
    CHECK(world_compact(&w) == 2);
    CHECK(w.n == 1);
    CHECK_NEAR(w.b[0].mass, 3.0, 1e-15);
    CHECK_NEAR(w.b[0].radius, sqrt(0.51), 1e-12);
    world_free(&w);
}

/* Reference: the documented merge rule, brute force. */
static int ref_merge(world *w) {
    int total = 0, merges;
    unsigned char *t = malloc((size_t)w->n);
    do {
        merges = 0;
        memset(t, 0, (size_t)w->n);
        for (int i = 0; i < w->n; i++) {
            if (!w->b[i].alive || t[i]) continue;
            for (int j = i + 1; j < w->n; j++) {
                body *p = &w->b[i], *q = &w->b[j];
                double r = p->radius + q->radius;
                if (!q->alive || t[j] || v2_len2(v2_sub(p->pos, q->pos)) >= r * r) continue;
                body *k = q->mass > p->mass ? q : p, *g = k == p ? q : p;
                double m = k->mass + g->mass;
                k->pos = v2_scale(v2_add(v2_scale(k->pos, k->mass), v2_scale(g->pos, g->mass)), 1 / m);
                k->vel = v2_scale(v2_add(v2_scale(k->vel, k->mass), v2_scale(g->vel, g->mass)), 1 / m);
                k->mass = m;
                k->radius = sqrt(k->radius * k->radius + g->radius * g->radius);
                g->alive = 0;
                g->mass = 0;
                t[i] = t[j] = 1;
                merges++;
                break;
            }
        }
        total += merges;
    } while (merges);
    free(t);
    return total;
}

static void test_grid_matches_brute(void) {
    world a, b;
    world_init(&a, 1000);
    world_init(&b, 1000);
    rng_state = 777u;
    for (int i = 0; i < 1000; i++) {
        body x = body_make(NULL, KIND_DUST, 0.1 + rnd(), 0.005 + 0.02 * rnd(),
                           v2(rnd() * 2 - 1, rnd() * 2 - 1), v2(rnd(), rnd()), 0);
        world_add(&a, &x);
        world_add(&b, &x);
    }
    b.b[999].pos = v2(-1e6, 3e5); /* a far outlier must not break the grid */
    a.b[999].pos = b.b[999].pos;
    int ng = collide_merge(&a), nb = ref_merge(&b);
    CHECK(ng > 50);
    CHECK(ng == nb);
    int same = 1;
    for (int i = 0; i < 1000; i++) {
        if (a.b[i].alive != b.b[i].alive) same = 0;
        if (a.b[i].alive && (a.b[i].mass != b.b[i].mass || a.b[i].pos.x != b.b[i].pos.x ||
                             a.b[i].radius != b.b[i].radius))
            same = 0;
    }
    CHECK(same);
    /* afterwards nothing overlaps */
    CHECK(ref_merge(&a) == 0);
    world_free(&a);
    world_free(&b);
}

static void test_predict_circular(void) {
    world w;
    world_init(&w, 4);
    body sun = body_make("sun", KIND_STAR, 1.0, 0.05, v2(0, 0), v2(0, 0), 0);
    world_add(&w, &sun);
    vec2 out[4000];
    int n = predict_path(&w, v2(1, 0), v2(0, 1), 2 * PI / 2000, 4000, out);
    CHECK(n == 4000);
    double worst = 0.0;
    for (int i = 0; i < n; i++) {
        double d = fabs(v2_len(out[i]) - 1.0);
        if (d > worst) worst = d;
    }
    CHECK(worst < 1e-3);
    CHECK(out[0].x == 1.0 && out[0].y == 0.0);
    /* a radial plunge stops on the body */
    n = predict_path(&w, v2(1, 0), v2(0, 0), 0.001, 4000, out);
    CHECK(n > 1 && n < 4000);
    CHECK(v2_len(out[n - 1]) < 0.05);
    world_free(&w);
}

static void test_energy_formula(void) {
    world w;
    world_init(&w, 4);
    w.G = 2.5;
    body a = body_make(NULL, KIND_STAR, 3.0, 0.1, v2(0, 0), v2(1, 0), 0);
    body b = body_make(NULL, KIND_STAR, 2.0, 0.1, v2(3, 4), v2(0, -2), 0);
    world_add(&w, &a);
    world_add(&w, &b);
    double expect = -2.5 * 3 * 2 / 5.0 + 0.5 * 3 * 1 + 0.5 * 2 * 4;
    CHECK_NEAR(world_energy(&w), expect, 1e-12);
    w.b[1].alive = 0;
    CHECK_NEAR(world_energy(&w), 1.5, 1e-12);
    world_free(&w);
}

static void test_direct_accel(void) {
    world w;
    world_init(&w, 4);
    w.soft = 0.5;
    body a = body_make(NULL, KIND_STAR, 2.0, 0.1, v2(0, 0), v2(0, 0), 0);
    body b = body_make(NULL, KIND_STAR, 1.0, 0.1, v2(1, 0), v2(0, 0), 0);
    world_add(&w, &a);
    world_add(&w, &b);
    gravity_compute(&w, GRAV_DIRECT, 0.0);
    double f = 1.0 / pow(1.25, 1.5);
    CHECK_NEAR(w.b[0].acc.x, 1.0 * f, 1e-14);
    CHECK_NEAR(w.b[1].acc.x, -2.0 * f, 1e-14);
    CHECK_NEAR(w.b[0].acc.y, 0.0, 1e-15);
    world_free(&w);
}

int main(void) {
    RUN(test_direct_accel);
    RUN(test_circular_orbit_period);
    RUN(test_energy_bounded);
    RUN(test_momentum_conserved);
    RUN(test_energy_formula);
    RUN(test_merge_pair);
    RUN(test_no_merge_when_apart);
    RUN(test_chain_merge);
    RUN(test_grid_matches_brute);
    RUN(test_predict_circular);
    return TEST_SUMMARY();
}
