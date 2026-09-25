/* test_scenario.c - tests for scenario.h / scenario.c */
#include "test.h"
#include "scenario.h"
#include "world.h"

#include <math.h>
#include <string.h>

/* Direct-sum kinetic + potential energy, same formula gravity_potential/gravity_direct would use,
   reimplemented locally so this test compiles against only scenario.c + world.c. */
static double total_energy(const world *w) {
    double ke = 0.0, pe = 0.0;
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        ke += 0.5 * w->bodies[i].mass * vec3_len2(w->bodies[i].vel);
    }
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        for (int j = i + 1; j < w->count; j++) {
            if (!w->bodies[j].alive) continue;
            double r2 = vec3_len2(vec3_sub(w->bodies[i].pos, w->bodies[j].pos)) + w->softening * w->softening;
            pe -= w->G * w->bodies[i].mass * w->bodies[j].mass / sqrt(r2);
        }
    }
    return ke + pe;
}

static double momentum_scale(const world *w) {
    double s = 0.0;
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        s += w->bodies[i].mass * vec3_len(w->bodies[i].vel);
    }
    return s;
}

static void test_names_roundtrip(void) {
    int n = scenario_count();
    CHECK(n == 7);
    for (int i = 0; i < n; i++) {
        const char *name = scenario_name(i);
        CHECK(name != NULL);
        world w;
        CHECK(world_init(&w, 8) == 0);
        int rc = scenario_load(&w, name, 1, NULL);
        CHECK(rc == 0);
        world_free(&w);
    }
    CHECK(scenario_name(-1) == NULL);
    CHECK(scenario_name(n) == NULL);
}

static void test_unknown_name(void) {
    world w;
    CHECK(world_init(&w, 4) == 0);
    int rc = scenario_load(&w, "not-a-real-scenario", 1, NULL);
    CHECK(rc == -1);
    world_free(&w);
}

static void test_body_counts(void) {
    struct {
        const char *name;
        int count;
    } cases[] = {
        {"solar", 10}, {"figure8", 3}, {"cluster", 400}, {"disk", 1501}, {"hohmann", 2},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        world w;
        CHECK(world_init(&w, 8) == 0);
        scenario_info info;
        memset(&info, 0, sizeof(info));
        int rc = scenario_load(&w, cases[i].name, 7, &info);
        CHECK(rc == 0);
        CHECK(world_alive(&w) == cases[i].count);
        CHECK(w.count == cases[i].count);
        CHECK(info.dt > 0.0);
        CHECK(info.duration > 0.0);
        CHECK(strlen(info.description) > 0);
        world_free(&w);
    }
}

static void test_com_frame(void) {
    const char *names[] = {"solar", "earth-moon", "binary", "figure8", "cluster", "disk", "hohmann"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        world w;
        CHECK(world_init(&w, 8) == 0);
        CHECK(scenario_load(&w, names[i], 3, NULL) == 0);
        vec3 p = world_momentum(&w);
        double scale = momentum_scale(&w);
        CHECK(vec3_len(p) <= 1e-6 * (scale > 0.0 ? scale : 1.0));
        world_free(&w);
    }
}

static void test_cluster_determinism(void) {
    world a, b, c;
    CHECK(world_init(&a, 8) == 0);
    CHECK(world_init(&b, 8) == 0);
    CHECK(world_init(&c, 8) == 0);
    CHECK(scenario_load(&a, "cluster", 123, NULL) == 0);
    CHECK(scenario_load(&b, "cluster", 123, NULL) == 0);
    CHECK(scenario_load(&c, "cluster", 456, NULL) == 0);

    CHECK(a.count == b.count);
    CHECK(a.count == c.count);

    int identical_ab = 1, identical_ac = 1;
    for (int i = 0; i < a.count; i++) {
        if (vec3_dist(a.bodies[i].pos, b.bodies[i].pos) > 1e-15) identical_ab = 0;
        if (vec3_dist(a.bodies[i].pos, c.bodies[i].pos) > 1e-15) identical_ac = 0;
    }
    CHECK(identical_ab == 1);
    CHECK(identical_ac == 0);

    world_free(&a);
    world_free(&b);
    world_free(&c);
}

static void test_figure8_energy(void) {
    world w;
    CHECK(world_init(&w, 4) == 0);
    CHECK(scenario_load(&w, "figure8", 1, NULL) == 0);
    CHECK(w.count == 3);
    CHECK_NEAR(w.G, 1.0, 1e-15);
    double e = total_energy(&w);
    CHECK_NEAR(e, -1.287, 0.01);
    world_free(&w);
}

static void test_solar_earth_speed(void) {
    world w;
    CHECK(world_init(&w, 16) == 0);
    CHECK(scenario_load(&w, "solar", 1, NULL) == 0);
    body *earth = world_find(&w, "Earth");
    CHECK(earth != NULL);
    if (earth) {
        double speed = vec3_len(earth->vel);
        CHECK_REL(speed, 29800.0, 0.02);
    }
    body *sun = world_find(&w, "Sun");
    CHECK(sun != NULL);
    body *moon = world_find(&w, "Moon");
    CHECK(moon != NULL);
    world_free(&w);
}

static void test_hohmann_ship(void) {
    world w;
    CHECK(world_init(&w, 4) == 0);
    CHECK(scenario_load(&w, "hohmann", 1, NULL) == 0);
    body *ship = world_find(&w, "ship");
    CHECK(ship != NULL);
    if (ship) CHECK(ship->kind == BODY_SPACECRAFT);
    world_free(&w);
}

int main(void) {
    RUN(test_names_roundtrip);
    RUN(test_unknown_name);
    RUN(test_body_counts);
    RUN(test_com_frame);
    RUN(test_cluster_determinism);
    RUN(test_figure8_energy);
    RUN(test_solar_earth_speed);
    RUN(test_hohmann_ship);
    return TEST_SUMMARY();
}
