#include <string.h>

#include "scenario.h"
#include "test.h"

#define PI 3.14159265358979323846

/* Direct-sum energy with the world's softening (scenario tests may not link physics.c). */
static double energy(const world *w) {
    double e = 0.0;
    for (int i = 0; i < w->n; i++) {
        e += 0.5 * w->b[i].mass * v2_len2(w->b[i].vel);
        for (int j = i + 1; j < w->n; j++) {
            double d2 = v2_len2(v2_sub(w->b[i].pos, w->b[j].pos)) + w->soft * w->soft;
            e -= w->G * w->b[i].mass * w->b[j].mass / sqrt(d2);
        }
    }
    return e;
}

static int find_named(const world *w, const char *name) {
    for (int i = 0; i < w->n; i++)
        if (strcmp(w->b[i].name, name) == 0) return i;
    return -1;
}

static void test_all_load(void) {
    static const int counts[7] = {10, 5, 3, 3001, 3002, 701, 9};
    world w;
    CHECK(world_init(&w, 16) == 0);
    CHECK(scenario_count() == 7);
    for (int i = 0; i < scenario_count(); i++) {
        scenario_view v;
        memset(&v, 0, sizeof v);
        CHECK(scenario_load(&w, i, 42, &v) == 0);
        CHECK(w.n == counts[i]);
        CHECK(strlen(scenario_name(i)) > 0);
        CHECK(strlen(scenario_desc(i)) > 0);
        CHECK(w.G > 0.0);
        CHECK(w.soft >= 0.0);
        CHECK(w.t == 0.0);
        /* Centre-of-mass frame. */
        double m = world_mass(&w);
        vec2 c = world_com(&w), p = world_momentum(&w);
        CHECK(v2_len(c) < 1e-9 * v.view_radius);
        CHECK(v2_len(p) / m < 1e-9);
        /* Sane view. */
        CHECK(v.view_radius > 0.0 && v.view_radius < 100.0);
        CHECK(v2_len(v.view_center) < v.view_radius);
        CHECK(v.dt > 0.0 && v.dt < 0.01);
        CHECK(v.steps_per_frame >= 1 && v.steps_per_frame <= 16);
        CHECK(v.gravity == GRAV_DIRECT || v.gravity == GRAV_BH);
        CHECK(v.time_unit != NULL);
        CHECK(v.time_per_unit > 0.0);
        /* Every body sane and most bodies within a few view radii. */
        int inside = 0;
        for (int k = 0; k < w.n; k++) {
            const body *b = &w.b[k];
            CHECK(b->alive && b->mass > 0.0 && b->radius > 0.0);
            CHECK(b->kind >= 0 && b->kind < KIND_COUNT);
            CHECK(isfinite(b->pos.x) && isfinite(b->vel.y));
            if (v2_len(b->pos) < 3.0 * v.view_radius) inside++;
        }
        CHECK(inside * 10 >= w.n * 7);
    }
    scenario_view v;
    CHECK(scenario_load(&w, -1, 1, &v) == -1);
    CHECK(scenario_load(&w, 7, 1, &v) == -1);
    world_free(&w);
}

static void test_expected_settings(void) {
    world w;
    scenario_view v;
    CHECK(world_init(&w, 16) == 0);
    CHECK(scenario_load(&w, 0, 1, &v) == 0);
    CHECK_REL(w.G, 4.0 * PI * PI, 1e-12);
    CHECK(v.dt == 0.0005 && v.steps_per_frame == 4 && v.view_radius == 1.8);
    CHECK(strcmp(v.time_unit, "years") == 0);
    CHECK(scenario_load(&w, 3, 1, &v) == 0);
    CHECK(v.gravity == GRAV_BH && w.soft == 0.02);
    CHECK(scenario_load(&w, 5, 1, &v) == 0);
    CHECK(v.collisions == 1);
    CHECK(scenario_load(&w, 2, 1, &v) == 0);
    CHECK(v.collisions == 0 && w.G == 1.0);
    world_free(&w);
}

static void test_seed_determinism(void) {
    world a, b;
    scenario_view v;
    CHECK(world_init(&a, 16) == 0);
    CHECK(world_init(&b, 16) == 0);
    for (int s = 3; s <= 5; s++) {
        CHECK(scenario_load(&a, s, 7, &v) == 0);
        CHECK(scenario_load(&b, s, 7, &v) == 0);
        CHECK(a.n == b.n);
        int same = 1;
        for (int i = 0; i < a.n; i++)
            if (memcmp(&a.b[i].pos, &b.b[i].pos, sizeof(vec2)) ||
                memcmp(&a.b[i].vel, &b.b[i].vel, sizeof(vec2)) || a.b[i].mass != b.b[i].mass ||
                a.b[i].color != b.b[i].color)
                same = 0;
        CHECK(same);
    }
    CHECK(scenario_load(&a, 3, 7, &v) == 0);
    CHECK(scenario_load(&b, 3, 8, &v) == 0);
    int differ = 0;
    for (int i = 0; i < a.n; i++)
        if (a.b[i].pos.x != b.b[i].pos.x) differ++;
    CHECK(differ > a.n / 2);
    world_free(&a);
    world_free(&b);
}

static void test_find(void) {
    CHECK(scenario_find("Solar System") == 0);
    CHECK(scenario_find("solar system") == 0);
    CHECK(scenario_find("GALAXY") == 3);
    CHECK(scenario_find("galaxy collision") == 4);
    CHECK(scenario_find("figure-eight") == 2);
    CHECK(scenario_find("earth") == 6);
    CHECK(scenario_find("3") == 2);
    CHECK(scenario_find("1") == 0);
    CHECK(scenario_find("7") == 6);
    CHECK(scenario_find("0") == -1);
    CHECK(scenario_find("8") == -1);
    CHECK(scenario_find("3x") == -1);
    CHECK(scenario_find("") == -1);
    CHECK(scenario_find("nonsense") == -1);
    CHECK(scenario_find(NULL) == -1);
    for (int i = 0; i < scenario_count(); i++) CHECK(scenario_find(scenario_name(i)) == i);
}

static void test_solar_system(void) {
    world w;
    scenario_view v;
    CHECK(world_init(&w, 16) == 0);
    CHECK(scenario_load(&w, 0, 1, &v) == 0);
    int sun = find_named(&w, "Sun"), earth = find_named(&w, "Earth"), moon = find_named(&w, "Moon");
    CHECK(sun >= 0 && earth >= 0 && moon >= 0);
    if (sun < 0 || earth < 0 || moon < 0) return;
    CHECK(w.b[sun].kind == KIND_STAR && w.b[sun].color == 0xFFF0C8u);
    CHECK_REL(v2_len(w.b[earth].vel), 2.0 * PI, 0.02);
    CHECK_REL(v2_dist(w.b[earth].pos, w.b[sun].pos), 1.0, 0.01);
    CHECK_REL(v2_dist(w.b[moon].pos, w.b[earth].pos), 0.00257, 1e-9);
    int nep = find_named(&w, "Neptune");
    CHECK(nep >= 0);
    if (nep >= 0) CHECK_REL(v2_dist(w.b[nep].pos, w.b[sun].pos), 30.07, 0.01);
    int planets = 0;
    for (int i = 0; i < w.n; i++) planets += w.b[i].kind == KIND_PLANET;
    CHECK(planets == 8);
    world_free(&w);
}

static void test_figure8_energy(void) {
    world w;
    scenario_view v;
    CHECK(world_init(&w, 16) == 0);
    CHECK(scenario_load(&w, 2, 1, &v) == 0);
    CHECK_NEAR(energy(&w), -1.287, 0.01);
    CHECK(w.b[0].color != w.b[1].color && w.b[1].color != w.b[2].color);
    world_free(&w);
}

static void test_bound_systems(void) {
    world w;
    scenario_view v;
    CHECK(world_init(&w, 16) == 0);
    /* Binary: separation at start is the periapsis a(1 - e) and the pair is bound. */
    CHECK(scenario_load(&w, 1, 1, &v) == 0);
    CHECK_REL(v2_dist(w.b[0].pos, w.b[1].pos), 0.7, 1e-9);
    CHECK(energy(&w) < 0.0);
    /* Galaxy: stars near r = 2 orbit at about the circular speed for the enclosed mass. */
    CHECK(scenario_load(&w, 3, 3, &v) == 0);
    int bh = -1;
    for (int i = 0; i < w.n; i++)
        if (w.b[i].kind == KIND_BLACKHOLE) bh = i;
    CHECK(bh >= 0);
    if (bh >= 0) {
        int dust = 0;
        for (int i = 0; i < w.n; i++) {
            if (i == bh) continue;
            dust += w.b[i].kind == KIND_DUST;
            vec2 d = v2_sub(w.b[i].pos, w.b[bh].pos);
            double r = v2_len(d);
            CHECK(r > 0.09 && r < 4.01);
            double vt = v2_cross(d, v2_sub(w.b[i].vel, w.b[bh].vel)) / r;
            CHECK(vt > 0.0 && vt * vt * r < 1.3 && vt * vt * r > 0.7);
        }
        CHECK(dust == 3000);
    }
    world_free(&w);
}

int main(void) {
    RUN(test_all_load);
    RUN(test_expected_settings);
    RUN(test_seed_determinism);
    RUN(test_find);
    RUN(test_solar_system);
    RUN(test_figure8_energy);
    RUN(test_bound_systems);
    return TEST_SUMMARY();
}
