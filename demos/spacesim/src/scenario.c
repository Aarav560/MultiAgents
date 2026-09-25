/* scenario.c - built-in universes. */
#include "scenario.h"
#include "rng.h"

#include <math.h>
#include <string.h>

#define SCENARIO_PI 3.14159265358979323846

/* ---- small helpers -------------------------------------------------- */

static void set_info(scenario_info *info, double dt, double duration, double scale, const char *desc) {
    if (!info) return;
    info->dt = dt;
    info->duration = duration;
    info->scale = scale;
    strncpy(info->description, desc, sizeof(info->description) - 1);
    info->description[sizeof(info->description) - 1] = '\0';
}

/* Circular orbit around a moving center: mu is G * central mass, r the orbital radius,
   inc the inclination (rad, rotation about the x-axis / line of nodes), phase the initial
   angle in the orbital plane (rad). */
static void circular_orbit_state(double mu, double r, double inc, double phase,
                                  vec3 center_pos, vec3 center_vel, vec3 *pos, vec3 *vel) {
    double v = sqrt(mu / r);
    double xp = r * cos(phase), yp = r * sin(phase);
    double vxp = -v * sin(phase), vyp = v * cos(phase);
    double ci = cos(inc), si = sin(inc);
    vec3 rel_pos = vec3_make(xp, yp * ci, yp * si);
    vec3 rel_vel = vec3_make(vxp, vyp * ci, vyp * si);
    *pos = vec3_add(center_pos, rel_pos);
    *vel = vec3_add(center_vel, rel_vel);
}

/* ---- solar ------------------------------------------------------------ */

typedef struct {
    const char *name;
    double mass, radius, a, inc_deg;
    rgb color;
} planet_data;

static const planet_data PLANETS[] = {
    {"Mercury", 3.301e23, 2.4397e6, 5.791e10, 7.0, {169, 169, 169}},
    {"Venus", 4.8675e24, 6.0518e6, 1.0821e11, 3.39, {230, 190, 138}},
    {"Earth", 5.972e24, 6.371e6, 1.496e11, 0.0, {70, 130, 180}},
    {"Mars", 6.417e23, 3.3895e6, 2.2794e11, 1.85, {193, 68, 14}},
    {"Jupiter", 1.898e27, 6.9911e7, 7.7857e11, 1.3, {216, 179, 130}},
    {"Saturn", 5.683e26, 5.8232e7, 1.4335e12, 2.49, {235, 214, 168}},
    {"Uranus", 8.681e25, 2.5362e7, 2.8725e12, 0.77, {172, 229, 238}},
    {"Neptune", 1.024e26, 2.4622e7, 4.4951e12, 1.77, {62, 84, 196}},
};
#define N_PLANETS ((int)(sizeof(PLANETS) / sizeof(PLANETS[0])))

static int scenario_solar(world *w, scenario_info *info) {
    double m_sun = 1.989e30, r_sun = 6.957e8;
    double mu_sun = G_SI * m_sun;

    body sun = body_make("Sun", BODY_STAR, m_sun, r_sun, vec3_zero(), vec3_zero());
    sun.color = (rgb){255, 214, 10};
    world_add(w, &sun);

    vec3 earth_pos = vec3_zero(), earth_vel = vec3_zero();
    int have_earth = 0;

    for (int i = 0; i < N_PLANETS; i++) {
        const planet_data *p = &PLANETS[i];
        double inc = p->inc_deg * (SCENARIO_PI / 180.0);
        double phase = i * 2.4;
        vec3 pos, vel;
        circular_orbit_state(mu_sun, p->a, inc, phase, vec3_zero(), vec3_zero(), &pos, &vel);
        body b = body_make(p->name, BODY_PLANET, p->mass, p->radius, pos, vel);
        b.color = p->color;
        world_add(w, &b);
        if (strcmp(p->name, "Earth") == 0) {
            earth_pos = pos;
            earth_vel = vel;
            have_earth = 1;
        }
    }

    if (have_earth) {
        double m_earth = PLANETS[2].mass; /* Earth entry above */
        double mu_earth = G_SI * m_earth;
        vec3 mpos, mvel;
        circular_orbit_state(mu_earth, 3.844e8, 0.0, 1.0, earth_pos, earth_vel, &mpos, &mvel);
        body moon = body_make("Moon", BODY_MOON, 7.342e22, 1.7374e6, mpos, mvel);
        moon.color = (rgb){200, 200, 200};
        world_add(w, &moon);
    }

    world_recenter(w);
    set_info(info, 3600.0, 365.25 * 86400.0, 4.4951e12 * 2.2 / 800.0,
              "Sun and eight planets plus the Moon, circular orbits, approximate inclinations");
    return 0;
}

/* ---- earth-moon -------------------------------------------------------- */

static int scenario_earth_moon(world *w, scenario_info *info) {
    double m_earth = 5.972e24, r_earth = 6.371e6;
    body earth = body_make("Earth", BODY_PLANET, m_earth, r_earth, vec3_zero(), vec3_zero());
    earth.color = (rgb){70, 130, 180};
    world_add(w, &earth);

    double mu = G_SI * m_earth;
    vec3 pos, vel;
    circular_orbit_state(mu, 3.844e8, 0.0, 0.0, vec3_zero(), vec3_zero(), &pos, &vel);
    body moon = body_make("Moon", BODY_MOON, 7.342e22, 1.7374e6, pos, vel);
    moon.color = (rgb){200, 200, 200};
    world_add(w, &moon);

    world_recenter(w);
    set_info(info, 60.0, 28.0 * 86400.0, 3.844e8 * 2.2 / 800.0,
              "Earth and the Moon orbiting their common center of mass");
    return 0;
}

/* ---- binary -------------------------------------------------------------- */

static int scenario_binary(world *w, scenario_info *info) {
    double m1 = 1.989e30, m2 = 1.989e30;
    double a = 1.496e11, e = 0.5;
    double mu = G_SI * (m1 + m2);
    double rp = a * (1.0 - e);
    double vp = sqrt(mu * (1.0 + e) / rp); /* vis-viva at periapsis */

    vec3 r_rel = vec3_make(rp, 0.0, 0.0);
    vec3 v_rel = vec3_make(0.0, vp, 0.0);
    double frac1 = m2 / (m1 + m2), frac2 = m1 / (m1 + m2);

    body starA = body_make("StarA", BODY_STAR, m1, 6.957e8, vec3_scale(r_rel, -frac1), vec3_scale(v_rel, -frac1));
    starA.color = (rgb){255, 214, 10};
    body starB = body_make("StarB", BODY_STAR, m2, 6.957e8, vec3_scale(r_rel, frac2), vec3_scale(v_rel, frac2));
    starB.color = (rgb){255, 140, 60};
    world_add(w, &starA);
    world_add(w, &starB);

    double r_planet = 4.0 * 1.496e11;
    double mu_total = G_SI * (m1 + m2);
    vec3 ppos, pvel;
    circular_orbit_state(mu_total, r_planet, 0.0, 0.5, vec3_zero(), vec3_zero(), &ppos, &pvel);
    body planet = body_make("Planet", BODY_PLANET, 5.972e24, 6.371e6, ppos, pvel);
    planet.color = (rgb){70, 150, 190};
    world_add(w, &planet);

    world_recenter(w);
    set_info(info, 3600.0, 3.0 * 365.25 * 86400.0, r_planet * 2.2 / 800.0,
              "Eccentric binary pair of solar-mass stars with a circumbinary planet");
    return 0;
}

/* ---- figure8 -------------------------------------------------------------- */

static int scenario_figure8(world *w, scenario_info *info) {
    w->G = 1.0;

    vec3 x1 = vec3_make(0.97000436, -0.24308753, 0.0);
    vec3 x2 = vec3_scale(x1, -1.0);
    vec3 x3 = vec3_zero();
    vec3 v3 = vec3_make(-0.93240737, -0.86473146, 0.0);
    vec3 v1 = vec3_scale(v3, -0.5);
    vec3 v2 = v1;

    body b1 = body_make("Body1", BODY_STAR, 1.0, 0.05, x1, v1);
    b1.color = (rgb){255, 80, 80};
    body b2 = body_make("Body2", BODY_STAR, 1.0, 0.05, x2, v2);
    b2.color = (rgb){80, 255, 80};
    body b3 = body_make("Body3", BODY_STAR, 1.0, 0.05, x3, v3);
    b3.color = (rgb){80, 80, 255};
    world_add(w, &b1);
    world_add(w, &b2);
    world_add(w, &b3);

    world_recenter(w);
    set_info(info, 0.001, 6.3259, 1.1 * 2.2 / 800.0, "Chenciner-Montgomery figure-eight three-body choreography");
    return 0;
}

/* ---- cluster ---------------------------------------------------------------- */

static int scenario_cluster(world *w, unsigned long long seed, scenario_info *info) {
    w->G = 1.0;
    w->softening = 0.05;

    rng r;
    rng_seed(&r, seed);

    const int n = 400;
    double mass = 1.0 / n;

    for (int i = 0; i < n; i++) {
        double x1 = rng_double(&r);
        if (x1 < 1e-9) x1 = 1e-9;
        double radius = 1.0 / sqrt(pow(x1, -2.0 / 3.0) - 1.0);

        double x2 = rng_double(&r), x3 = rng_double(&r);
        double costheta = 1.0 - 2.0 * x2;
        double s2 = 1.0 - costheta * costheta;
        if (s2 < 0.0) s2 = 0.0;
        double sintheta = sqrt(s2);
        double phi = 2.0 * SCENARIO_PI * x3;
        vec3 pos = vec3_make(radius * sintheta * cos(phi), radius * sintheta * sin(phi), radius * costheta);

        double q = 0.0, g = 0.0;
        for (;;) {
            double x4 = rng_double(&r), x5 = rng_double(&r);
            q = x4;
            g = q * q * pow(1.0 - q * q, 3.5);
            if (x5 * 0.1 <= g) break;
        }
        double ve = sqrt(2.0) * pow(1.0 + radius * radius, -0.25);
        double speed = q * ve;

        double x6 = rng_double(&r), x7 = rng_double(&r);
        double costheta_v = 1.0 - 2.0 * x6;
        double sv2 = 1.0 - costheta_v * costheta_v;
        if (sv2 < 0.0) sv2 = 0.0;
        double sintheta_v = sqrt(sv2);
        double phiv = 2.0 * SCENARIO_PI * x7;
        vec3 vel = vec3_make(speed * sintheta_v * cos(phiv), speed * sintheta_v * sin(phiv), speed * costheta_v);

        body b = body_make("star", BODY_STAR, mass, 0.01, pos, vel);
        b.color = (rgb){200, 200, 255};
        world_add(w, &b);
    }

    world_recenter(w);
    set_info(info, 0.01, 10.0, 6.0 * 2.2 / 800.0, "400-body Plummer-sphere star cluster");
    return 0;
}

/* ---- disk --------------------------------------------------------------------- */

static int scenario_disk(world *w, unsigned long long seed, scenario_info *info) {
    w->G = 1.0;
    w->softening = 0.01;

    double m_central = 1.0;
    body star = body_make("Star", BODY_STAR, m_central, 0.05, vec3_zero(), vec3_zero());
    star.color = (rgb){255, 230, 120};
    world_add(w, &star);

    rng r;
    rng_seed(&r, seed);
    double mu = w->G * m_central;

    for (int i = 0; i < 1500; i++) {
        double rad = rng_range(&r, 0.5, 3.0);
        double phase = rng_range(&r, 0.0, 2.0 * SCENARIO_PI);
        double v = sqrt(mu / rad);
        vec3 pos = vec3_make(rad * cos(phase), rad * sin(phase), 0.0);
        vec3 vel = vec3_make(-v * sin(phase), v * cos(phase), 0.0);
        body b = body_make("particle", BODY_PARTICLE, 1e-7, 0.001, pos, vel);
        b.color = (rgb){150, 180, 255};
        world_add(w, &b);
    }

    world_recenter(w);
    set_info(info, 0.005, 20.0, 6.0 * 2.2 / 800.0, "1500 test particles in a disk around a central mass");
    return 0;
}

/* ---- hohmann ------------------------------------------------------------------- */

static int scenario_hohmann(world *w, scenario_info *info) {
    double m_earth = 5.972e24, r_earth = 6.371e6;
    body earth = body_make("Earth", BODY_PLANET, m_earth, r_earth, vec3_zero(), vec3_zero());
    earth.color = (rgb){70, 130, 180};
    world_add(w, &earth);

    double mu = G_SI * m_earth;
    double r = 7.0e6; /* ~7000 km circular parking orbit */
    vec3 pos, vel;
    circular_orbit_state(mu, r, 0.0, 0.0, vec3_zero(), vec3_zero(), &pos, &vel);
    body ship = body_make("ship", BODY_SPACECRAFT, 1000.0, 10.0, pos, vel);
    ship.color = (rgb){255, 255, 255};
    world_add(w, &ship);

    world_recenter(w);
    set_info(info, 10.0, 6.0 * 3600.0, r * 2.2 / 800.0,
              "Earth and a spacecraft in a 7000 km circular parking orbit");
    return 0;
}

/* ---- dispatch ------------------------------------------------------------------- */

static const char *SCENARIO_NAMES[] = {
    "solar", "earth-moon", "binary", "figure8", "cluster", "disk", "hohmann",
};
#define N_SCENARIOS ((int)(sizeof(SCENARIO_NAMES) / sizeof(SCENARIO_NAMES[0])))

int scenario_load(world *w, const char *name, unsigned long long seed, scenario_info *info) {
    if (!w || !name) return -1;
    if (strcmp(name, "solar") == 0) return scenario_solar(w, info);
    if (strcmp(name, "earth-moon") == 0) return scenario_earth_moon(w, info);
    if (strcmp(name, "binary") == 0) return scenario_binary(w, info);
    if (strcmp(name, "figure8") == 0) return scenario_figure8(w, info);
    if (strcmp(name, "cluster") == 0) return scenario_cluster(w, seed, info);
    if (strcmp(name, "disk") == 0) return scenario_disk(w, seed, info);
    if (strcmp(name, "hohmann") == 0) return scenario_hohmann(w, info);
    return -1;
}

int scenario_count(void) { return N_SCENARIOS; }

const char *scenario_name(int index) {
    if (index < 0 || index >= N_SCENARIOS) return NULL;
    return SCENARIO_NAMES[index];
}
