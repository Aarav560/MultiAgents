/* scenario.c - the 7 built-in universes. */
#include "scenario.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#define PI 3.14159265358979323846
#define G_AU (4.0 * PI * PI) /* AU^3 / (Msun yr^2) */
#define NUM_SCENARIOS 7

static const char *const names[NUM_SCENARIOS] = {
    "Solar System", "Binary Star",      "Figure Eight",  "Galaxy",
    "Galaxy Collision", "Planet Formation", "Earth and Moon",
};

static const char *const descs[NUM_SCENARIOS] = {
    "The Sun, the 8 planets at their real distances and masses, and the Moon",
    "An eccentric binary star with three circumbinary planets",
    "Three equal masses chasing each other on the Chenciner-Montgomery figure eight",
    "A two-armed spiral galaxy of 3000 stars around a central black hole",
    "Two spiral galaxies on a parabolic encounter, tearing each other into tidal tails",
    "A young star with 700 planetesimals that collide and merge into planets",
    "Earth, the Moon, six satellites and a probe on an escape trajectory",
};

/* ---- seeded PRNG (splitmix64) ------------------------------------------ */

typedef struct {
    uint64_t s;
} rng;

static uint64_t rng_next(rng *r) {
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Uniform in [0, 1). */
static double rng_unit(rng *r) { return (double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0); }

static double rng_range(rng *r, double lo, double hi) { return lo + (hi - lo) * rng_unit(r); }

static double rng_gauss(rng *r) {
    double u1 = 1.0 - rng_unit(r), u2 = rng_unit(r);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
}

/* ---- helpers ------------------------------------------------------------ */

static uint32_t rgb(int r, int g, int b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int clamp255(double v) { return v < 0.0 ? 0 : v > 255.0 ? 255 : (int)(v + 0.5); }

/* Linear blend of two colors (t in [0, 1]) scaled by brightness. */
static uint32_t color_mix(uint32_t a, uint32_t b, double t, double bright) {
    double ch[3];
    for (int k = 0; k < 3; k++) {
        int sh = 16 - 8 * k;
        double ca = (double)((a >> sh) & 0xFF), cb = (double)((b >> sh) & 0xFF);
        ch[k] = (ca + (cb - ca) * t) * bright;
    }
    return rgb(clamp255(ch[0]), clamp255(ch[1]), clamp255(ch[2]));
}

static vec2 polar(double r, double ang) { return v2(r * cos(ang), r * sin(ang)); }

/* Speed of a circular orbit of radius r around mass m (Plummer-softened by soft). */
static double v_circ(double G, double m, double r, double soft) {
    double d2 = r * r + soft * soft;
    return sqrt(G * m * r * r / (d2 * sqrt(d2)));
}

static int add(world *w, const char *name, body_kind kind, double mass, double radius, vec2 pos,
               vec2 vel, uint32_t color) {
    body b = body_make(name, kind, mass, radius, pos, vel, color);
    return world_add(w, &b);
}

/* Adds a body on a circular orbit around body index c (counter-clockwise). */
static int add_orbiter(world *w, int c, const char *name, body_kind kind, double mass,
                       double radius, double dist, double phase, uint32_t color) {
    const body *p = &w->b[c];
    vec2 dir = polar(1.0, phase);
    double v = v_circ(w->G, p->mass + mass, dist, 0.0);
    vec2 pos = v2_madd(p->pos, dir, dist), vel = v2_madd(p->vel, v2_perp(dir), v);
    return add(w, name, kind, mass, radius, pos, vel, color);
}

static void set_view(scenario_view *v, double radius, double dt, int steps, gravity_mode g,
                     int collisions, const char *unit, double per_unit) {
    v->view_center = v2(0.0, 0.0);
    v->view_radius = radius;
    v->dt = dt;
    v->steps_per_frame = steps;
    v->gravity = g;
    v->collisions = collisions;
    v->time_unit = unit;
    v->time_per_unit = per_unit;
}

/* ---- 1. Solar System ---------------------------------------------------- */

typedef struct {
    const char *name;
    double a, mass, radius;
    uint32_t color;
} planet_data;

static const planet_data planets[8] = {
    {"Mercury", 0.387, 1.66e-7, 0.0040, 0xB5A89Au},
    {"Venus", 0.723, 2.45e-6, 0.0058, 0xE8C98Au},
    {"Earth", 1.000, 3.0e-6, 0.0060, 0x4A8CFFu},
    {"Mars", 1.524, 3.23e-7, 0.0048, 0xE0502Eu},
    {"Jupiter", 5.203, 9.55e-4, 0.0100, 0xD8B48Au},
    {"Saturn", 9.537, 2.86e-4, 0.0092, 0xE8D590u},
    {"Uranus", 19.19, 4.37e-5, 0.0075, 0x7FE6F0u},
    {"Neptune", 30.07, 5.15e-5, 0.0074, 0x3456E0u},
};

static int load_solar(world *w, scenario_view *v) {
    w->G = G_AU;
    w->soft = 0.0;
    int sun = add(w, "Sun", KIND_STAR, 1.0, 0.02, v2(0, 0), v2(0, 0), rgb(255, 240, 200));
    if (sun < 0) return -1;
    for (int i = 0; i < 8; i++) {
        const planet_data *p = &planets[i];
        double phase = i * 2.39996322972865332; /* golden angle spreads the phases */
        int k = add_orbiter(w, sun, p->name, KIND_PLANET, p->mass, p->radius, p->a, phase,
                            p->color);
        if (k < 0) return -1;
        if (i == 2) {
            /* Put the Earth-Moon barycentre on the orbit: shift Earth by the Moon's share. */
            const double mm = 3.69e-8, d = 0.00257;
            body *e = &w->b[k];
            double me = e->mass, f = mm / (me + mm);
            vec2 dir = polar(1.0, phase + 1.0);
            double vrel = v_circ(w->G, me + mm, d, 0.0);
            vec2 bpos = e->pos, bvel = e->vel;
            e->pos = v2_madd(bpos, dir, -d * f);
            e->vel = v2_madd(bvel, v2_perp(dir), -vrel * f);
            vec2 mpos = v2_madd(bpos, dir, d * (1.0 - f));
            vec2 mvel = v2_madd(bvel, v2_perp(dir), vrel * (1.0 - f));
            if (add(w, "Moon", KIND_MOON, mm, 0.002, mpos, mvel, rgb(200, 200, 205)) < 0)
                return -1;
        }
    }
    set_view(v, 1.8, 0.0005, 4, GRAV_DIRECT, 0, "years", 1.0);
    return 0;
}

/* ---- 2. Binary Star ----------------------------------------------------- */

static int load_binary(world *w, scenario_view *v) {
    w->G = G_AU;
    w->soft = 0.0;
    const double m1 = 1.0, m2 = 0.6, a = 1.0, e = 0.3, mt = m1 + m2;
    /* Start at periapsis: separation a(1-e), relative speed from vis-viva. */
    double r = a * (1.0 - e), vr = sqrt(w->G * mt * (1.0 + e) / (a * (1.0 - e)));
    if (add(w, "Alpha", KIND_STAR, m1, 0.05, v2(-r * m2 / mt, 0), v2(0, -vr * m2 / mt),
            rgb(255, 225, 170)) < 0)
        return -1;
    if (add(w, "Beta", KIND_STAR, m2, 0.04, v2(r * m1 / mt, 0), v2(0, vr * m1 / mt),
            rgb(170, 200, 255)) < 0)
        return -1;
    static const struct {
        const char *name;
        double a, mass, phase;
        uint32_t color;
    } pl[3] = {
        {"Tatoo", 3.0, 3e-6, 0.8, 0x6FC7A0u},
        {"Kepler", 4.2, 3e-5, 3.1, 0xD9A36Bu},
        {"Nomad", 5.5, 1e-4, 4.9, 0x9A8CF0u},
    };
    for (int i = 0; i < 3; i++) {
        vec2 dir = polar(1.0, pl[i].phase);
        double vc = v_circ(w->G, mt, pl[i].a, 0.0);
        if (add(w, pl[i].name, KIND_PLANET, pl[i].mass, 0.02, v2_scale(dir, pl[i].a),
                v2_scale(v2_perp(dir), vc), pl[i].color) < 0)
            return -1;
    }
    set_view(v, 7.0, 0.0005, 4, GRAV_DIRECT, 0, "years", 1.0);
    return 0;
}

/* ---- 3. Figure Eight ---------------------------------------------------- */

static int load_figure8(world *w, scenario_view *v) {
    w->G = 1.0;
    w->soft = 0.0;
    vec2 x1 = v2(0.97000436, -0.24308753), v3 = v2(-0.93240737, -0.86473146);
    vec2 v1 = v2_scale(v3, -0.5);
    if (add(w, "A", KIND_STAR, 1.0, 0.03, x1, v1, rgb(255, 120, 90)) < 0) return -1;
    if (add(w, "B", KIND_STAR, 1.0, 0.03, v2_scale(x1, -1.0), v1, rgb(110, 220, 140)) < 0)
        return -1;
    if (add(w, "C", KIND_STAR, 1.0, 0.03, v2(0, 0), v3, rgb(120, 160, 255)) < 0) return -1;
    set_view(v, 1.4, 0.001, 8, GRAV_DIRECT, 0, "", 1.0);
    return 0;
}

/* ---- 4/5. Galaxy disks -------------------------------------------------- */

#define DISK_RMIN 0.1
#define DISK_RMAX 4.0
#define DISK_STAR_MASS 2e-5
#define GALAXY_SOFT 0.02

/* Mass fraction of an exponential disk (scale 1) inside r: 1 - (1 + r) e^-r. */
static double exp_cdf(double r) { return 1.0 - (1.0 + r) * exp(-r); }

typedef struct {
    vec2 center, vel;
    int n;
    int spin;             /* +1 counter-clockwise, -1 clockwise */
    double arm_phase;
    uint32_t core, outer; /* tint from core to outskirts */
    const char *bh_name;
} disk_spec;

/* Surface density ~ r e^-r: sample Gamma(2, 1), keeping only [RMIN, RMAX]. */
static double disk_radius(rng *g) {
    for (;;) {
        double r = -log((1.0 - rng_unit(g)) * (1.0 - rng_unit(g)));
        if (r >= DISK_RMIN && r <= DISK_RMAX) return r;
    }
}

/* Angle on a two-armed trailing logarithmic spiral (pitch ~22 deg), or uniform off-arm. */
static double disk_angle(rng *g, double r, double phase, int spin, int *on_arm) {
    *on_arm = r > 0.35 && rng_unit(g) < 0.7;
    if (!*on_arm) return rng_range(g, 0.0, 2.0 * PI);
    const double wind = 2.47; /* 1 / tan(pitch) */
    double arm = rng_unit(g) < 0.5 ? 0.0 : PI;
    return phase + arm - spin * wind * log(r / DISK_RMIN) + 0.28 * rng_gauss(g);
}

static int add_disk(world *w, rng *g, const disk_spec *d) {
    const double bh_mass = 1.0, mdisk = d->n * DISK_STAR_MASS;
    const double c0 = exp_cdf(DISK_RMIN), c1 = exp_cdf(DISK_RMAX);
    if (add(w, d->bh_name, KIND_BLACKHOLE, bh_mass, 0.02, d->center, d->vel, rgb(255, 150, 60)) <
        0)
        return -1;
    for (int i = 0; i < d->n; i++) {
        double r = disk_radius(g);
        int on_arm;
        double ang = disk_angle(g, r, d->arm_phase, d->spin, &on_arm);
        double menc = bh_mass + mdisk * (exp_cdf(r) - c0) / (c1 - c0);
        double vc = v_circ(w->G, menc, r, GALAXY_SOFT) * (1.0 + 0.03 * rng_gauss(g));
        vec2 dir = polar(1.0, ang);
        vec2 pos = v2_madd(d->center, dir, r);
        vec2 vel = v2_madd(d->vel, v2_perp(dir), d->spin * vc);
        vel = v2_madd(vel, dir, 0.02 * vc * rng_gauss(g));
        double t = (r - DISK_RMIN) / 2.5 + (on_arm ? 0.2 : 0.0);
        t = t > 1.0 ? 1.0 : t;
        uint32_t col = color_mix(d->core, d->outer, t, rng_range(g, 0.6, 1.0));
        if (add(w, NULL, KIND_DUST, DISK_STAR_MASS, 0.004, pos, vel, col) < 0) return -1;
    }
    return 0;
}

static int load_galaxy(world *w, unsigned seed, scenario_view *v) {
    rng g = {(uint64_t)seed * 0x2545F4914F6CDD1Dull + 4};
    w->G = 1.0;
    w->soft = GALAXY_SOFT;
    disk_spec d = {v2(0, 0), v2(0, 0), 3000, 1, rng_range(&g, 0.0, 2.0 * PI),
                   rgb(255, 222, 150), rgb(150, 185, 255), "Sgr A*"};
    if (add_disk(w, &g, &d) < 0) return -1;
    set_view(v, 4.5, 0.002, 2, GRAV_BH, 0, "", 1.0);
    return 0;
}

static int load_collision(world *w, unsigned seed, scenario_view *v) {
    rng g = {(uint64_t)seed * 0x2545F4914F6CDD1Dull + 5};
    w->G = 1.0;
    w->soft = GALAXY_SOFT;
    const double m = 1.0 + 1500 * DISK_STAR_MASS, d = 8.0, q = 1.5;
    /* Parabolic relative orbit: v^2 = 2GM/d, specific angular momentum sqrt(2GMq). */
    double mt = 2.0 * m, vrel = sqrt(2.0 * w->G * mt / d);
    double vt = sqrt(2.0 * w->G * mt * q) / d, vr = sqrt(vrel * vrel - vt * vt);
    vec2 rel = polar(d, 0.35), rhat = v2_norm(rel);
    vec2 vr_vec = v2_add(v2_scale(rhat, -vr), v2_scale(v2_perp(rhat), vt));
    /* The second disk counter-rotates: the 2D stand-in for a strongly inclined encounter. */
    disk_spec a = {v2_scale(rel, -0.5), v2_scale(vr_vec, -0.5), 1500, 1,
                   rng_range(&g, 0.0, 2.0 * PI), rgb(255, 228, 165), rgb(140, 180, 255), "Core A"};
    disk_spec b = {v2_scale(rel, 0.5), v2_scale(vr_vec, 0.5), 1500, -1,
                   rng_range(&g, 0.0, 2.0 * PI), rgb(255, 200, 140), rgb(255, 130, 190), "Core B"};
    if (add_disk(w, &g, &a) < 0 || add_disk(w, &g, &b) < 0) return -1;
    set_view(v, 9.0, 0.004, 2, GRAV_BH, 0, "", 1.0);
    return 0;
}

/* ---- 6. Planet Formation ------------------------------------------------ */

static int load_formation(world *w, unsigned seed, scenario_view *v) {
    rng g = {(uint64_t)seed * 0x2545F4914F6CDD1Dull + 6};
    w->G = G_AU;
    w->soft = 0.002;
    int star = add(w, "Protostar", KIND_STAR, 1.0, 0.03, v2(0, 0), v2(0, 0), rgb(255, 214, 160));
    if (star < 0) return -1;
    for (int i = 0; i < 700; i++) {
        double mass = 3e-8 * pow(10.0, rng_unit(&g)); /* log-uniform 3e-8 .. 3e-7 */
        double radius = 0.01 * cbrt(mass / 3e-7);
        double r = rng_range(&g, 0.4, 3.0), ang = rng_range(&g, 0.0, 2.0 * PI);
        double vc = v_circ(w->G, 1.0, r, 0.0);
        vec2 dir = polar(1.0, ang);
        /* Small eccentricity (~0.02): perturb tangential and radial speed. */
        vec2 vel = v2_add(v2_scale(v2_perp(dir), vc * (1.0 + 0.01 * rng_gauss(&g))),
                          v2_scale(dir, vc * 0.01 * rng_gauss(&g)));
        double shade = rng_range(&g, 0.55, 1.0);
        uint32_t col = color_mix(rgb(190, 150, 110), rgb(170, 175, 185), rng_unit(&g), shade);
        body_kind kind = mass > 1e-7 ? KIND_ASTEROID : KIND_DUST;
        if (add(w, NULL, kind, mass, radius, v2_scale(dir, r), vel, col) < 0) return -1;
    }
    set_view(v, 3.5, 0.001, 4, GRAV_DIRECT, 1, "years", 1.0);
    return 0;
}

/* ---- 7. Earth and Moon -------------------------------------------------- */

static int load_earth_moon(world *w, scenario_view *v) {
    w->G = 1.0;
    w->soft = 0.0;
    const double mm = 0.0123;
    int earth = add(w, "Earth", KIND_PLANET, 1.0, 0.05, v2(0, 0), v2(0, 0), rgb(70, 140, 255));
    if (earth < 0) return -1;
    if (add_orbiter(w, earth, "Moon", KIND_MOON, mm, 0.015, 1.0, 0.0, rgb(205, 205, 210)) < 0)
        return -1;
    static const struct {
        const char *name;
        double r, phase, boost; /* speed as a multiple of circular */
        uint32_t color;
    } sats[7] = {
        {"ISS", 0.07, 0.5, 1.0, 0xFFFFFFu},
        {"Hubble", 0.09, 2.6, 1.0, 0xFFE08Au},
        {"Molniya", 0.08, 4.2, 1.25, 0xFF8A8Au},
        {"GPS", 0.2, 1.3, 1.0, 0x9AFFB0u},
        {"GEO", 0.3, 3.7, 1.0, 0x8AD8FFu},
        {"Relay", 0.45, 0.2, 1.0, 0xC8A0FFu},
        {"Voyager", 0.1, 5.4, 1.6, 0xFFB45Au}, /* > sqrt(2): escapes */
    };
    const body *e = &w->b[earth];
    for (int i = 0; i < 7; i++) {
        vec2 dir = polar(1.0, sats[i].phase);
        double vc = v_circ(w->G, e->mass, sats[i].r, 0.0) * sats[i].boost;
        vec2 pos = v2_madd(e->pos, dir, sats[i].r), vel = v2_madd(e->vel, v2_perp(dir), vc);
        if (add(w, sats[i].name, KIND_SHIP, 1e-12, 0.003, pos, vel, sats[i].color) < 0) return -1;
    }
    /* One lunar orbit (2 pi / sqrt(G(1 + mm))) lasts 27.32 days. */
    double per_day = 2.0 * PI / sqrt(w->G * (1.0 + mm)) / 27.32;
    set_view(v, 1.4, 0.0005, 8, GRAV_DIRECT, 0, "days", per_day);
    return 0;
}

/* ---- public API ---------------------------------------------------------- */

int scenario_count(void) { return NUM_SCENARIOS; }

const char *scenario_name(int index) {
    return index >= 0 && index < NUM_SCENARIOS ? names[index] : "";
}

const char *scenario_desc(int index) {
    return index >= 0 && index < NUM_SCENARIOS ? descs[index] : "";
}

int scenario_load(world *w, int index, unsigned seed, scenario_view *view) {
    if (index < 0 || index >= NUM_SCENARIOS) return -1;
    scenario_view dummy;
    if (!view) view = &dummy;
    world_clear(w);
    int rc;
    switch (index) {
    case 0: rc = load_solar(w, view); break;
    case 1: rc = load_binary(w, view); break;
    case 2: rc = load_figure8(w, view); break;
    case 3: rc = load_galaxy(w, seed, view); break;
    case 4: rc = load_collision(w, seed, view); break;
    case 5: rc = load_formation(w, seed, view); break;
    default: rc = load_earth_moon(w, view); break;
    }
    if (rc < 0) return -1;
    world_recenter(w);
    return 0;
}

/* Lowercase letters and digits only, so "figure-eight" matches "Figure Eight". */
static void normalize(const char *s, char *out, size_t cap) {
    size_t n = 0;
    for (; *s && n + 1 < cap; s++)
        if (isalnum((unsigned char)*s)) out[n++] = (char)tolower((unsigned char)*s);
    out[n] = '\0';
}

int scenario_find(const char *name_or_number) {
    if (!name_or_number) return -1;
    const char *s = name_or_number;
    while (isspace((unsigned char)*s)) s++;
    if (isdigit((unsigned char)*s)) {
        int num = 0;
        while (isdigit((unsigned char)*s) && num < 1000) num = num * 10 + (*s++ - '0');
        while (isspace((unsigned char)*s)) s++;
        return (*s == '\0' && num >= 1 && num <= NUM_SCENARIOS) ? num - 1 : -1;
    }
    char key[64], cand[64];
    normalize(s, key, sizeof key);
    if (key[0] == '\0') return -1;
    int prefix = -1, prefix_hits = 0;
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        normalize(names[i], cand, sizeof cand);
        size_t k = 0;
        while (key[k] && key[k] == cand[k]) k++;
        if (key[k] == '\0' && cand[k] == '\0') return i;
        if (key[k] == '\0') {
            prefix = i;
            prefix_hits++;
        }
    }
    return prefix_hits == 1 ? prefix : -1;
}
