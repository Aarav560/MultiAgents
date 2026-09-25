#include "config.h"
#include "diagnostics.h"
#include "sim.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static simulation *make_sim(const char *scenario, const char *integrator, double duration) {
    sim_config cfg;
    char err[256];
    config_defaults(&cfg);
    CHECK(config_set(&cfg, "scenario", scenario) == 0);
    CHECK(config_set(&cfg, "integrator", integrator) == 0);
    cfg.duration = duration;
    cfg.quiet = 1;
    simulation *s = sim_create(&cfg, err, sizeof(err));
    if (!s) fprintf(stderr, "  sim_create: %s\n", err);
    return s;
}

static int index_of(const world *w, const char *name) {
    for (int i = 0; i < w->count; i++) {
        if (w->bodies[i].alive && strcmp(w->bodies[i].name, name) == 0) return i;
    }
    return -1;
}

static void test_figure8_period(void) {
    simulation *s = make_sim("figure8", "yoshida", 6.3259);
    CHECK(s != NULL);
    if (!s) return;
    const world *w = sim_world(s);
    CHECK(w->count == 3);
    vec3 start[3];
    for (int i = 0; i < 3; i++) start[i] = w->bodies[i].pos;
    diag d0, d1;
    diagnostics_compute(w, &d0);
    CHECK(sim_run(s, NULL) == 0);
    diagnostics_compute(w, &d1);
    CHECK(diagnostics_rel_drift(d0.total, d1.total) < 1e-6);
    CHECK_NEAR(w->t, 6.3259, 1e-3);
    for (int i = 0; i < 3; i++) CHECK(vec3_dist(w->bodies[i].pos, start[i]) < 1e-2);
    sim_destroy(s);
}

static void test_solar_earth_orbit(void) {
    simulation *s = make_sim("solar", "leapfrog", 30.0 * 86400.0);
    CHECK(s != NULL);
    if (!s) return;
    CHECK(sim_run(s, NULL) == 0);
    const world *w = sim_world(s);
    int sun = index_of(w, "Sun"), earth = index_of(w, "Earth");
    CHECK(sun >= 0 && earth >= 0);
    if (sun >= 0 && earth >= 0) {
        double r = vec3_dist(w->bodies[sun].pos, w->bodies[earth].pos);
        CHECK_REL(r, 1.495978707e11, 0.02);
    }
    CHECK_NEAR(w->t, 30.0 * 86400.0, 1.0);
    sim_destroy(s);
}

static void test_bad_config(void) {
    sim_config cfg;
    char err[256] = "";
    config_defaults(&cfg);
    strcpy(cfg.scenario, "nope");
    CHECK(sim_create(&cfg, err, sizeof(err)) == NULL);
    CHECK(err[0] != '\0');
    config_defaults(&cfg);
    strcpy(cfg.integrator, "magic");
    CHECK(sim_create(&cfg, err, sizeof(err)) == NULL);
}

static void test_hohmann_reaches_geo(void) {
    simulation *s = make_sim("hohmann", "leapfrog", 0.0);
    CHECK(s != NULL);
    if (!s) return;
    CHECK(sim_run(s, NULL) == 0);
    const world *w = sim_world(s);
    int ship = index_of(w, "ship"), earth = index_of(w, "Earth");
    CHECK(ship >= 0 && earth >= 0);
    if (ship >= 0 && earth >= 0) {
        CHECK_REL(vec3_dist(w->bodies[ship].pos, w->bodies[earth].pos), 42164e3, 0.01);
    }
    sim_destroy(s);
}

int main(void) {
    RUN(test_figure8_period);
    RUN(test_solar_earth_orbit);
    RUN(test_bad_config);
    RUN(test_hohmann_reaches_geo);
    return TEST_SUMMARY();
}
