#define _POSIX_C_SOURCE 200809L
#include "sim.h"

#include "collision.h"
#include "diagnostics.h"
#include "gravity.h"
#include "integrator.h"
#include "octree.h"
#include "output_csv.h"
#include "render_ascii.h"
#include "render_ppm.h"
#include "scenario.h"
#include "spacecraft.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define ASCII_COLS 100
#define ASCII_ROWS 40
#define HOHMANN_TARGET 42164e3
#define HOHMANN_T1 60.0

struct simulation {
    sim_config cfg;
    world w;
    scenario_info info;
    double dt;
    long steps;
    long every;
    accel_fn accel;
    bh_params bh;
    integrator_kind ik;
    csv_writer *csv;
    image im;
    int have_image;
    char *ascii_buf;
    flight_plan plan;
    int is_hohmann;
    hohmann ho;
    int burns_done;
    double t_burn1;
    long merges;
};

static void set_err(char *err, size_t errlen, const char *msg, const char *arg) {
    if (err && errlen > 0) snprintf(err, errlen, "%s%s", msg, arg ? arg : "");
}

/* mkdir -p: creates every component of path. */
static int mkdir_p(const char *path) {
    char buf[512];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
            buf[i] = c;
        }
    }
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

static int find_index(const world *w, const char *name) {
    for (int i = 0; i < w->count; i++) {
        if (w->bodies[i].alive && strcmp(w->bodies[i].name, name) == 0) return i;
    }
    return -1;
}

static double ship_radius(const world *w) {
    int si = find_index(w, "ship"), ei = find_index(w, "Earth");
    if (si < 0 || ei < 0) return -1.0;
    return vec3_dist(w->bodies[si].pos, w->bodies[ei].pos);
}

static int setup_hohmann(simulation *s, char *err, size_t errlen) {
    int ei = find_index(&s->w, "Earth");
    double r1 = ship_radius(&s->w);
    if (ei < 0 || r1 <= 0.0) {
        set_err(err, errlen, "hohmann scenario lacks Earth or ship", NULL);
        return -1;
    }
    double mu = s->w.G * s->w.bodies[ei].mass;
    if (hohmann_plan(mu, r1, HOHMANN_TARGET, &s->ho) != 0) {
        set_err(err, errlen, "hohmann_plan failed", NULL);
        return -1;
    }
    s->is_hohmann = 1;
    return 0;
}

static int setup_outputs(simulation *s, char *err, size_t errlen) {
    const sim_config *c = &s->cfg;
    if (c->csv_path[0]) {
        s->csv = csv_open(c->csv_path);
        if (!s->csv) {
            set_err(err, errlen, "cannot open CSV file ", c->csv_path);
            return -1;
        }
    }
    if (c->ppm_dir[0]) {
        if (mkdir_p(c->ppm_dir) != 0) {
            set_err(err, errlen, "cannot create PPM directory ", c->ppm_dir);
            return -1;
        }
        if (image_init(&s->im, c->width, c->height) != 0) {
            set_err(err, errlen, "cannot allocate image", NULL);
            return -1;
        }
        s->have_image = 1;
    }
    if (c->ascii) {
        s->ascii_buf = malloc((size_t)ASCII_ROWS * (ASCII_COLS + 1) + 1);
        if (!s->ascii_buf) {
            set_err(err, errlen, "out of memory", NULL);
            return -1;
        }
    }
    return 0;
}

static int setup_run(simulation *s, char *err, size_t errlen) {
    const sim_config *c = &s->cfg;
    if (integrator_parse(c->integrator, &s->ik) != 0) {
        set_err(err, errlen, "unknown integrator ", c->integrator);
        return -1;
    }
    if (strcmp(c->gravity, "direct") == 0) {
        s->accel = gravity_direct;
    } else if (strcmp(c->gravity, "bh") == 0 || strcmp(c->gravity, "barnes-hut") == 0) {
        s->accel = gravity_barnes_hut;
        s->bh.theta = c->theta;
    } else {
        set_err(err, errlen, "unknown gravity ", c->gravity);
        return -1;
    }
    s->dt = c->dt > 0.0 ? c->dt : s->info.dt;
    double duration = c->duration > 0.0 ? c->duration : s->info.duration;
    if (!(s->dt > 0.0)) {
        set_err(err, errlen, "dt must be positive", NULL);
        return -1;
    }
    if (c->steps > 0) {
        s->steps = c->steps;
    } else {
        double n = floor(duration / s->dt + 0.5);
        s->steps = n < 1.0 ? 1 : (long)n;
    }
    s->every = c->output_every > 0 ? c->output_every : (s->steps / 200 > 0 ? s->steps / 200 : 1);
    if (c->softening >= 0.0) s->w.softening = c->softening;
    if (c->scale <= 0.0) s->cfg.scale = s->info.scale * 800.0 / (c->width > 0 ? c->width : 800);
    return 0;
}

simulation *sim_create(const sim_config *cfg, char *err, size_t errlen) {
    if (!cfg) {
        set_err(err, errlen, "no configuration", NULL);
        return NULL;
    }
    simulation *s = calloc(1, sizeof(*s));
    if (!s) {
        set_err(err, errlen, "out of memory", NULL);
        return NULL;
    }
    s->cfg = *cfg;
    flight_plan_init(&s->plan);
    if (world_init(&s->w, 16) != 0) {
        free(s);
        set_err(err, errlen, "out of memory", NULL);
        return NULL;
    }
    if (scenario_load(&s->w, cfg->scenario, cfg->seed, &s->info) != 0) {
        set_err(err, errlen, "unknown scenario ", cfg->scenario);
        sim_destroy(s);
        return NULL;
    }
    if (setup_run(s, err, errlen) != 0 || setup_outputs(s, err, errlen) != 0 ||
        (strcmp(cfg->scenario, "hohmann") == 0 && setup_hohmann(s, err, errlen) != 0)) {
        sim_destroy(s);
        return NULL;
    }
    return s;
}

void sim_destroy(simulation *s) {
    if (!s) return;
    if (s->csv) csv_close(s->csv);
    if (s->have_image) image_free(&s->im);
    free(s->ascii_buf);
    flight_plan_free(&s->plan);
    world_free(&s->w);
    free(s);
}

const world *sim_world(const simulation *s) {
    return s ? &s->w : NULL;
}

/* Hohmann burns: prograde impulse dv1 at t >= 60 s, then dv2 once the transfer arc is flown. */
static void hohmann_burns(simulation *s) {
    double t_due = s->burns_done == 0 ? HOHMANN_T1 : s->t_burn1 + s->ho.tof;
    if (s->burns_done >= 2 || s->w.t < t_due) return;
    int si = find_index(&s->w, "ship"), ei = find_index(&s->w, "Earth");
    if (si < 0 || ei < 0) {
        s->burns_done = 2;
        return;
    }
    double mag = s->burns_done == 0 ? s->ho.dv1 : s->ho.dv2;
    vec3 dv = vec3_scale(spacecraft_prograde(&s->w, si, ei), mag);
    s->w.bodies[si].vel = vec3_add(s->w.bodies[si].vel, dv);
    int k = flight_plan_add(&s->plan, s->w.t, s->w.bodies[si].id, dv);
    if (k >= 0) s->plan.burns[k].done = 1;
    if (s->burns_done == 0) s->t_burn1 = s->w.t;
    s->burns_done++;
}

static int write_outputs(simulation *s, long frame, FILE *log, double e0) {
    const sim_config *c = &s->cfg;
    if (s->csv && csv_write_frame(s->csv, &s->w) != 0) return -1;
    if (s->have_image) {
        char path[600];
        if (c->trails) image_fade(&s->im, 0.92);
        else image_fill(&s->im, (rgb){0, 0, 0});
        if (render_world(&s->im, &s->w, c->scale) != 0) return -1;
        snprintf(path, sizeof(path), "%s/frame_%06ld.ppm", c->ppm_dir, frame);
        if (image_write_ppm(&s->im, path) != 0) return -1;
    }
    if (s->ascii_buf && log) {
        double cell = c->scale * (c->width > 0 ? c->width : 800) / ASCII_COLS;
        size_t len = (size_t)ASCII_ROWS * (ASCII_COLS + 1) + 1;
        if (render_ascii(&s->w, s->ascii_buf, len, ASCII_COLS, ASCII_ROWS, cell) == 0) {
            fprintf(log, "\033[H\033[2J%s", s->ascii_buf);
        }
    }
    if (!c->quiet && log) {
        diag d;
        diagnostics_compute(&s->w, &d);
        fprintf(log, "t=%.6g step=%llu bodies=%d E=%.10g dE/E0=%.3e\n", s->w.t, s->w.step,
                world_alive(&s->w), d.total, diagnostics_rel_drift(e0, d.total));
    }
    return 0;
}

static double momentum_scale(const world *w) {
    double sum = 0.0;
    for (int i = 0; i < w->count; i++) {
        if (w->bodies[i].alive) sum += w->bodies[i].mass * vec3_len(w->bodies[i].vel);
    }
    return sum > 0.0 ? sum : 1e-300;
}

static void print_summary(simulation *s, FILE *log, const diag *d0, double pscale, double wall) {
    diag d;
    diagnostics_compute(&s->w, &d);
    fprintf(log, "summary: scenario=%s integrator=%s gravity=%s dt=%g\n", s->cfg.scenario,
            integrator_name(s->ik), s->cfg.gravity, s->dt);
    fprintf(log, "  steps=%ld wall=%.3fs steps/s=%.0f\n", s->steps, wall,
            wall > 0.0 ? (double)s->steps / wall : 0.0);
    fprintf(log, "  energy drift=%.3e momentum drift=%.3e merges=%ld bodies=%d\n",
            diagnostics_rel_drift(d0->total, d.total),
            vec3_len(vec3_sub(d.momentum, d0->momentum)) / pscale, s->merges, world_alive(&s->w));
    if (s->is_hohmann) {
        fprintf(log, "  hohmann: burns=%d delta-v=%.2f m/s ship radius=%.1f km (target %.1f km)\n",
                s->burns_done, spacecraft_delta_v_budget(&s->plan), ship_radius(&s->w) / 1e3,
                HOHMANN_TARGET / 1e3);
    }
}

int sim_run(simulation *s, FILE *log) {
    if (!s) return -1;
    diag d0;
    diagnostics_compute(&s->w, &d0);
    double pscale = momentum_scale(&s->w);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    void *ctx = s->accel == gravity_barnes_hut ? (void *)&s->bh : NULL;
    long frame = 0;
    int rc = 0;
    for (long i = 1; i <= s->steps && rc == 0; i++) {
        if (s->is_hohmann) hohmann_burns(s);
        flight_plan_apply(&s->plan, &s->w, s->w.t, s->w.t + s->dt);
        if (integrator_step(&s->w, s->ik, s->dt, s->accel, ctx) != 0) {
            rc = -1;
            break;
        }
        if (s->cfg.collisions) {
            int m = collision_merge(&s->w);
            if (m < 0) rc = -1;
            else if (m > 0) {
                s->merges += m;
                world_compact(&s->w);
            }
        }
        if (rc == 0 && (i % s->every == 0 || i == s->steps)) {
            rc = write_outputs(s, frame++, log, d0.total);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double wall = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    if (log) print_summary(s, log, &d0, pscale, wall);
    return rc;
}
