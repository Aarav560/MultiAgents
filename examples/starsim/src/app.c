/* app.c - the interactive simulator: state, main loop, input, physics pacing and drawing. */
#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "font.h"
#include "platform.h"
#include "render.h"
#include "scenario.h"
#include "sim.h"
#include "trails.h"

#define DEFAULT_W 1280
#define DEFAULT_H 800
#define DEFAULT_SEED 12345u
#define TRAIL_BODIES 2048
#define TRAIL_POINTS 300
#define PREDICT_POINTS 600
#define BH_THETA 0.6
#define PHYS_BUDGET 0.012
#define PICK_PX 10.0
#define ZOOM_STEP 1.15
#define WARP_MIN 0.125
#define WARP_MAX 512.0
#define FOLLOW_T 0.2

#define BG_COLOR rgb(4, 6, 12)
#define HUD_BG rgb(10, 14, 28)
#define HUD_TEXT rgb(220, 230, 255)
#define BH_RING rgb(255, 150, 60)

typedef struct {
    const char *name;
    body_kind kind;
    double mass_frac;
    uint32_t color;
} launch_kind;

static const launch_kind launch_kinds[] = {
    {"asteroid", KIND_ASTEROID, 1e-9, 0xB4AA9Au},
    {"planet", KIND_PLANET, 1e-6, 0x5AA0FFu},
    {"star", KIND_STAR, 0.3, 0xFFE6B4u},
    {"black hole", KIND_BLACKHOLE, 2.0, 0xFF963Cu},
};
#define LAUNCH_KIND_COUNT ((int)(sizeof launch_kinds / sizeof launch_kinds[0]))

static const char *help_text =
    "CONTROLS\n"
    "\n"
    "left-drag       pan\n"
    "mouse wheel     zoom around cursor\n"
    "left-click      select / follow body\n"
    "click empty     stop following\n"
    "right-drag      launch a body\n"
    "1..7            load scenario\n"
    "space           pause / resume\n"
    "+ / -           time warp x2 / /2\n"
    "[ and ]         kind to launch\n"
    "t               toggle trails\n"
    "g               direct / Barnes-Hut\n"
    "c               toggle collisions\n"
    "f               fit system in view\n"
    "r               reload scenario\n"
    "h or F1         this help\n"
    "Esc             quit";

typedef struct {
    world w;
    scenario_view view;
    camera cam;
    canvas cv;
    trails *tr;
    vec2 *pred;             /* predict_path output, PREDICT_POINTS */
    vec2 *trail_buf;        /* trails_get output, TRAIL_POINTS */
    int scenario;
    unsigned seed;
    int windowed;

    gravity_mode grav;
    int collisions, trails_on, paused, help;
    double warp;
    int slowed;
    int launch_kind;
    int launched;

    int selected_id;        /* followed and highlighted body, or -1 */
    int panning;
    int pan_x, pan_y;
    int launching;
    int launch_sx, launch_sy;
    vec2 launch_pos;

    double fps;
    long frame;
    double e0, drift;
    int drift_valid;
} app_state;

static app_state app;

/* ---- setup ------------------------------------------------------------- */

static void reset_energy(void) {
    app.drift_valid = 0;
    app.e0 = app.w.n <= 3000 ? world_energy(&app.w) : 0.0;
}

static int load_scenario(int index) {
    if (scenario_load(&app.w, index, app.seed, &app.view) != 0) return -1;
    app.scenario = index;
    app.grav = app.view.gravity;
    app.collisions = app.view.collisions;
    app.warp = 1.0;
    app.paused = 0;
    app.slowed = 0;
    app.selected_id = -1;
    app.launching = 0;
    app.panning = 0;
    trails_clear(app.tr);
    cam_fit(&app.cam, app.view.view_center, app.view.view_radius);
    reset_energy();
    if (app.windowed) {
        char title[96];
        snprintf(title, sizeof title, "starsim - %s", scenario_name(index));
        pf_set_title(title);
    }
    return 0;
}

static int app_init(int w, int h, unsigned seed, int windowed) {
    memset(&app, 0, sizeof app);
    app.seed = seed;
    app.windowed = windowed;
    app.trails_on = 1;
    app.fps = 60.0;
    app.cam.w = w;
    app.cam.h = h;
    app.cam.scale = 1.0;
    if (world_init(&app.w, 1024) != 0) return -1;
    if (canvas_init(&app.cv, w, h) != 0) return -1;
    app.tr = trails_create(TRAIL_BODIES, TRAIL_POINTS);
    app.pred = malloc(sizeof(vec2) * PREDICT_POINTS);
    app.trail_buf = malloc(sizeof(vec2) * TRAIL_POINTS);
    if (!app.tr || !app.pred || !app.trail_buf) return -1;
    return 0;
}

static void app_shutdown(void) {
    trails_free(app.tr);
    free(app.pred);
    free(app.trail_buf);
    canvas_free(&app.cv);
    world_free(&app.w);
    memset(&app, 0, sizeof app);
}

/* ---- actions ----------------------------------------------------------- */

static int heaviest_index(void) {
    int best = -1;
    for (int i = 0; i < app.w.n; i++) {
        if (!app.w.b[i].alive) continue;
        if (best < 0 || app.w.b[i].mass > app.w.b[best].mass) best = i;
    }
    return best;
}

/* Launch speed: dragging d world units launches a body that covers d in about half a second
   of real time at 1x warp. */
static vec2 launch_velocity(int mx, int my) {
    double sim_per_sec = app.view.dt * app.view.steps_per_frame * 60.0;
    double launch_speed = sim_per_sec > 0.0 ? 2.0 / sim_per_sec : 1.0;
    vec2 drag = v2((double)(mx - app.launch_sx), (double)(app.launch_sy - my));
    vec2 v = v2_scale(drag, launch_speed / app.cam.scale);
    int f = app.selected_id >= 0 ? world_index_of(&app.w, app.selected_id) : -1;
    if (f >= 0) v = v2_add(v, app.w.b[f].vel);   /* relative to the followed body */
    return v;
}

static void launch_body(int mx, int my) {
    int hi = heaviest_index();
    const launch_kind *k = &launch_kinds[app.launch_kind];
    double big_m = hi >= 0 ? app.w.b[hi].mass : 1.0;
    double big_r = hi >= 0 ? app.w.b[hi].radius : 0.01;
    double m = big_m * k->mass_frac;
    double r = big_r * cbrt(k->mass_frac);   /* same density as the heaviest body */
    char name[BODY_NAME_MAX];
    snprintf(name, sizeof name, "%s %d", k->name, ++app.launched);
    body b = body_make(name, k->kind, m, r, app.launch_pos, launch_velocity(mx, my), k->color);
    if (world_add(&app.w, &b) >= 0) reset_energy();
}

static void fit_all(void) {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    int any = 0;
    for (int i = 0; i < app.w.n; i++) {
        const body *b = &app.w.b[i];
        if (!b->alive) continue;
        any = 1;
        x0 = fmin(x0, b->pos.x);
        y0 = fmin(y0, b->pos.y);
        x1 = fmax(x1, b->pos.x);
        y1 = fmax(y1, b->pos.y);
    }
    if (!any) return;
    double r = 0.5 * fmax(x1 - x0, y1 - y0) * 1.1;
    cam_fit(&app.cam, v2(0.5 * (x0 + x1), 0.5 * (y0 + y1)), r > 0.0 ? r : 1.0);
    app.selected_id = -1;
}

/* ---- input ------------------------------------------------------------- */

static void handle_keys(const pf_input *in) {
    const unsigned char *k = in->key_pressed;
    for (int i = 0; i < scenario_count() && i < 9; i++) {
        if (k['1' + i]) load_scenario(i);
    }
    if (k[' ']) app.paused = !app.paused;
    if (k['+'] || k['=']) app.warp = fmin(app.warp * 2.0, WARP_MAX);
    if (k['-']) app.warp = fmax(app.warp * 0.5, WARP_MIN);
    if (k['[']) app.launch_kind = (app.launch_kind + LAUNCH_KIND_COUNT - 1) % LAUNCH_KIND_COUNT;
    if (k[']']) app.launch_kind = (app.launch_kind + 1) % LAUNCH_KIND_COUNT;
    if (k['t']) app.trails_on = !app.trails_on;
    if (k['g']) app.grav = app.grav == GRAV_DIRECT ? GRAV_BH : GRAV_DIRECT;
    if (k['c']) app.collisions = !app.collisions;
    if (k['f']) fit_all();
    if (k['r']) load_scenario(app.scenario);
    if (k['h'] || k[PF_KEY_F1]) app.help = !app.help;
}

static void handle_mouse(const pf_input *in) {
    vec2 mouse = v2(in->mouse_x, in->mouse_y);
    if (in->wheel != 0.0) cam_zoom_at(&app.cam, mouse, pow(ZOOM_STEP, in->wheel));

    if (in->mouse_pressed[PF_MOUSE_LEFT]) {
        int i = world_nearest(&app.w, cam_to_world(&app.cam, mouse), PICK_PX / app.cam.scale);
        if (i >= 0) {
            app.selected_id = app.w.b[i].id;
        } else {
            app.selected_id = -1;
            app.panning = 1;
            app.pan_x = in->mouse_x;
            app.pan_y = in->mouse_y;
        }
    }
    if (app.panning && in->mouse_down[PF_MOUSE_LEFT]) {
        cam_pan_pixels(&app.cam, in->mouse_x - app.pan_x, in->mouse_y - app.pan_y);
        app.pan_x = in->mouse_x;
        app.pan_y = in->mouse_y;
    }
    if (!in->mouse_down[PF_MOUSE_LEFT]) app.panning = 0;

    if (in->mouse_pressed[PF_MOUSE_RIGHT]) {
        app.launching = 1;
        app.launch_sx = in->mouse_x;
        app.launch_sy = in->mouse_y;
        app.launch_pos = cam_to_world(&app.cam, mouse);
    }
    if (app.launching && (in->mouse_released[PF_MOUSE_RIGHT] || !in->mouse_down[PF_MOUSE_RIGHT])) {
        launch_body(in->mouse_x, in->mouse_y);
        app.launching = 0;
    }
}

/* Returns 0 when the app should quit. */
static int handle_input(const pf_input *in) {
    if (in->quit || in->key_pressed[PF_KEY_ESCAPE]) return 0;
    if (in->resized && in->w > 0 && in->h > 0 && canvas_resize(&app.cv, in->w, in->h) == 0) {
        app.cam.w = in->w;
        app.cam.h = in->h;
    }
    handle_keys(in);
    handle_mouse(in);
    return 1;
}

/* ---- physics ----------------------------------------------------------- */

static void step_physics(void) {
    app.slowed = 0;
    if (!app.paused && app.w.n > 0) {
        double dt = app.view.dt;
        int steps = app.view.steps_per_frame;
        if (app.warp >= 1.0) steps = (int)(steps * app.warp);
        else dt *= app.warp;
        double start = pf_time();
        for (int s = 0; s < steps; s++) {
            sim_step(&app.w, dt, app.grav, BH_THETA);
            if (s + 1 < steps && pf_time() - start > PHYS_BUDGET) {
                app.slowed = 1;
                break;
            }
        }
        if (app.collisions && collide_merge(&app.w) > 0) {
            world_compact(&app.w);
            reset_energy();
        }
        trails_record(app.tr, &app.w);
    }
    if (app.selected_id >= 0) {
        int i = world_index_of(&app.w, app.selected_id);
        if (i < 0) app.selected_id = -1;
        else cam_follow(&app.cam, app.w.b[i].pos, FOLLOW_T);
    }
    app.frame++;
    if (app.frame % 30 == 0 && app.w.n <= 3000 && app.w.n > 0) {
        double e = world_energy(&app.w);
        if (app.e0 != 0.0) {
            app.drift = fabs((e - app.e0) / app.e0);
            app.drift_valid = 1;
        }
    }
}

/* ---- drawing ----------------------------------------------------------- */

static uint32_t darker(uint32_t c) {
    return rgb(rgb_r(c) * 3 / 5, rgb_g(c) * 3 / 5, rgb_b(c) * 3 / 5);
}

static double min_px(body_kind k) {
    switch (k) {
    case KIND_STAR: return 4.0;
    case KIND_PLANET: return 3.5;
    case KIND_MOON: return 2.2;
    case KIND_SHIP: return 2.0;
    default: return 1.0;
    }
}

static double drawn_radius(const body *b) {
    return fmax(b->radius * app.cam.scale, min_px(b->kind));
}

static void draw_trails(void) {
    for (int i = 0; i < app.w.n; i++) {
        const body *b = &app.w.b[i];
        if (!b->alive || b->kind == KIND_DUST) continue;
        int n = trails_get(app.tr, b->id, app.trail_buf, TRAIL_POINTS);
        if (n < 2) continue;
        vec2 prev = cam_to_screen(&app.cam, app.trail_buf[0]);
        for (int k = 1; k < n; k++) {
            vec2 p = cam_to_screen(&app.cam, app.trail_buf[k]);
            int alpha = 150 * k / (n - 1);
            if (alpha > 0) canvas_line(&app.cv, prev.x, prev.y, p.x, p.y, b->color, alpha);
            prev = p;
        }
    }
}

static void draw_body(const body *b) {
    canvas *c = &app.cv;
    vec2 p = cam_to_screen(&app.cam, b->pos);
    double r = drawn_radius(b);
    double reach = b->kind == KIND_STAR ? fmax(12.0, 6.0 * r) : r + 2.0;
    if (p.x < -reach || p.y < -reach || p.x > c->w + reach || p.y > c->h + reach) return;
    switch (b->kind) {
    case KIND_STAR:
        canvas_glow(c, p.x, p.y, fmax(12.0, 6.0 * r), b->color, 0.9);
        canvas_disc(c, p.x, p.y, r, b->color, 255);
        break;
    case KIND_BLACKHOLE:
        canvas_disc(c, p.x, p.y, r, 0, 255);
        canvas_ring(c, p.x, p.y, r + 1.0, BH_RING, 255);
        break;
    case KIND_PLANET:
    case KIND_MOON:
        canvas_glow(c, p.x, p.y, 3.0 * r + 3.0, b->color, 0.22); /* faint halo so small planets read */
        canvas_disc(c, p.x, p.y, r, b->color, 255);
        canvas_ring(c, p.x, p.y, r, darker(b->color), 200);
        break;
    case KIND_DUST:
    case KIND_ASTEROID:
        if (r > 1.5) {
            canvas_disc(c, p.x, p.y, r, b->color, 255);
        } else { /* additive soft splat: dense star fields build up a glowing core */
            int x = (int)floor(p.x), y = (int)floor(p.y);
            canvas_add(c, x, y, b->color, 0.95);
            canvas_add(c, x + 1, y, b->color, 0.28);
            canvas_add(c, x - 1, y, b->color, 0.28);
            canvas_add(c, x, y + 1, b->color, 0.28);
            canvas_add(c, x, y - 1, b->color, 0.28);
        }
        break;
    default:
        canvas_disc(c, p.x, p.y, r, b->color, 255);
        break;
    }
}

static void draw_selection(void) {
    int i = app.selected_id >= 0 ? world_index_of(&app.w, app.selected_id) : -1;
    if (i < 0) return;
    const body *b = &app.w.b[i];
    vec2 p = cam_to_screen(&app.cam, b->pos);
    double r = drawn_radius(b) + 5.0;
    char label[BODY_NAME_MAX + 16];
    if (b->name[0]) snprintf(label, sizeof label, "%s", b->name);
    else snprintf(label, sizeof label, "#%d", b->id);
    canvas_ring(&app.cv, p.x, p.y, r, rgb(255, 255, 255), 200);
    font_draw(&app.cv, (int)(p.x + r + 4.0), (int)(p.y - 4.0), label, rgb(255, 255, 255), 1);
}

static void draw_world(void) {
    const camera *cam = &app.cam;
    canvas_clear(&app.cv, BG_COLOR);
    canvas_starfield(&app.cv, app.seed, -cam->center.x * cam->scale * 0.05,
                     cam->center.y * cam->scale * 0.05);
    if (app.trails_on) draw_trails();
    for (int i = 0; i < app.w.n; i++) {
        if (app.w.b[i].alive) draw_body(&app.w.b[i]);
    }
    draw_selection();
}

static void draw_launch_preview(const pf_input *in) {
    if (!app.launching || !in) return;
    vec2 a = cam_to_screen(&app.cam, app.launch_pos);
    uint32_t col = launch_kinds[app.launch_kind].color;
    canvas_line(&app.cv, a.x, a.y, in->mouse_x, in->mouse_y, rgb(255, 255, 255), 200);
    canvas_disc(&app.cv, a.x, a.y, 3.0, col, 255);
    double dt = app.view.dt * app.view.steps_per_frame;
    int n = predict_path(&app.w, app.launch_pos, launch_velocity(in->mouse_x, in->mouse_y), dt,
                         PREDICT_POINTS, app.pred);
    for (int k = 1; k < n; k += 2) {   /* every other segment: a dotted line */
        vec2 p0 = cam_to_screen(&app.cam, app.pred[k - 1]);
        vec2 p1 = cam_to_screen(&app.cam, app.pred[k]);
        canvas_line(&app.cv, p0.x, p0.y, p1.x, p1.y, col, 170);
    }
}

/* Scientific notation without the C library's padded exponent: 1.2e-5. */
static void fmt_sci(char *out, size_t n, double v) {
    if (v == 0.0) {
        snprintf(out, n, "0");
        return;
    }
    int e = (int)floor(log10(fabs(v)));
    double m = v / pow(10.0, e);
    if (fabs(m) >= 9.95) {
        m /= 10.0;
        e++;
    }
    snprintf(out, n, "%.1fe%d", m, e);
}

static void fmt_warp(char *out, size_t n) {
    if (app.paused) snprintf(out, n, "PAUSED");
    else if (app.warp >= 1.0) snprintf(out, n, "warp %gx", app.warp);
    else snprintf(out, n, "warp 1/%gx", 1.0 / app.warp);
}

static void draw_panel(int x, int y, const char *text, int scale, int alpha) {
    int lines = 1;
    for (const char *s = text; *s; s++) lines += *s == '\n';
    int w = font_width(text, scale) + 16;
    int h = lines * 8 * scale + (lines - 1) * 2 + 16;
    canvas_rect(&app.cv, x, y, w, h, HUD_BG, alpha);
    font_draw(&app.cv, x + 8, y + 8, text, HUD_TEXT, scale);
}

static void draw_hud(void) {
    char text[512], t[64], warp[32], drift[64];
    const scenario_view *v = &app.view;
    double per = v->time_per_unit > 0.0 ? v->time_per_unit : 1.0;
    const char *unit = v->time_unit ? v->time_unit : "";
    snprintf(t, sizeof t, "t = %.2f%s%s", app.w.t / per, unit[0] ? " " : "", unit);
    fmt_warp(warp, sizeof warp);
    drift[0] = 0;
    if (app.drift_valid && app.w.n <= 3000) {
        char num[32];
        fmt_sci(num, sizeof num, app.drift);
        snprintf(drift, sizeof drift, "\nenergy drift %s", num);
    }
    snprintf(text, sizeof text,
             "%s\n%s\nbodies %d\n%s%s\nfps %d\ngravity: %s\ncollisions: %s\nlaunch: %s%s",
             scenario_name(app.scenario), t, app.w.n, warp, app.slowed ? " (slowed)" : "",
             (int)(app.fps + 0.5), app.grav == GRAV_BH ? "Barnes-Hut" : "direct",
             app.collisions ? "on" : "off", launch_kinds[app.launch_kind].name, drift);
    draw_panel(8, 8, text, 2, 170);
    font_draw(&app.cv, 8, app.cv.h - 16, "H: help", HUD_TEXT, 1);
}

static void draw_help(void) {
    if (!app.help) return;
    int scale = 2;
    int lines = 1;
    for (const char *s = help_text; *s; s++) lines += *s == '\n';
    if (font_width(help_text, 2) + 16 > app.cv.w || lines * 18 + 16 > app.cv.h) scale = 1;
    int w = font_width(help_text, scale) + 16;
    int h = lines * 8 * scale + (lines - 1) * 2 + 16;
    draw_panel((app.cv.w - w) / 2, (app.cv.h - h) / 2, help_text, scale, 220);
}

static void draw_frame(const pf_input *in) {
    draw_world();
    draw_launch_preview(in);
    draw_hud();
    draw_help();
}

/* ---- entry points ------------------------------------------------------ */

static int render_frames(int scenario, int w, int h, int frames, const char *ppm_path,
                         unsigned seed) {
    if (w <= 0 || h <= 0 || !ppm_path) return -1;
    int rc = -1;
    if (app_init(w, h, seed, 0) == 0 && load_scenario(scenario) == 0) {
        for (int f = 0; f < frames; f++) step_physics();
        draw_frame(NULL);
        rc = canvas_write_ppm(&app.cv, ppm_path);
    }
    app_shutdown();
    return rc;
}

int app_render_frames(int scenario, int w, int h, int frames, const char *ppm_path) {
    return render_frames(scenario, w, h, frames, ppm_path, DEFAULT_SEED);
}

static void run_loop(void) {
    pf_input in;
    memset(&in, 0, sizeof in);
    in.w = app.cv.w;
    in.h = app.cv.h;
    double last = pf_time();
    for (;;) {
        double frame_start = pf_time();
        pf_poll(&in);
        if (!handle_input(&in)) break;
        step_physics();
        draw_frame(&in);
        pf_present(app.cv.px, app.cv.w, app.cv.h);
        double now = pf_time();
        double left = 1.0 / 60.0 - (now - frame_start);
        if (left > 0.0) pf_sleep(left);
        now = pf_time();
        if (now > last) app.fps = app.fps * 0.9 + 0.1 / (now - last);
        last = now;
    }
}

static void usage(FILE *f) {
    fprintf(f,
            "usage: starsim [--scenario N|name] [--width W] [--height H] [--seed S]\n"
            "               [--shot FILE.ppm --frames N] [--help]\n"
            "scenarios:\n");
    for (int i = 0; i < scenario_count(); i++)
        fprintf(f, "  %d  %-18s %s\n", i + 1, scenario_name(i), scenario_desc(i));
}

typedef struct {
    int scenario, w, h, frames;
    unsigned seed;
    const char *shot;
} options;

/* 0 ok, 1 help printed, 2 error. */
static int parse_args(int argc, char **argv, options *o) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 1;
        }
        if (!val) {
            fprintf(stderr, "starsim: unknown or incomplete option '%s'\n", a);
            return 2;
        }
        i++;
        if (!strcmp(a, "--scenario")) {
            o->scenario = scenario_find(val);
            if (o->scenario < 0) {
                fprintf(stderr, "starsim: unknown scenario '%s'\n", val);
                return 2;
            }
        } else if (!strcmp(a, "--width")) {
            o->w = atoi(val);
        } else if (!strcmp(a, "--height")) {
            o->h = atoi(val);
        } else if (!strcmp(a, "--seed")) {
            o->seed = (unsigned)strtoul(val, NULL, 10);
        } else if (!strcmp(a, "--shot")) {
            o->shot = val;
        } else if (!strcmp(a, "--frames")) {
            o->frames = atoi(val);
        } else {
            fprintf(stderr, "starsim: unknown option '%s'\n", a);
            return 2;
        }
    }
    if (o->w < 64 || o->h < 64 || o->w > 16384 || o->h > 16384 || o->frames < 0) {
        fprintf(stderr, "starsim: bad size or frame count\n");
        return 2;
    }
    return 0;
}

int app_main(int argc, char **argv) {
    options o = {0, DEFAULT_W, DEFAULT_H, 60, DEFAULT_SEED, NULL};
    int rc = parse_args(argc, argv, &o);
    if (rc == 1) return 0;
    if (rc == 2) {
        usage(stderr);
        return 2;
    }
    if (o.shot) {
        if (render_frames(o.scenario, o.w, o.h, o.frames, o.shot, o.seed) != 0) {
            fprintf(stderr, "starsim: could not render %s\n", o.shot);
            return 1;
        }
        printf("wrote %s (%dx%d, %s, %d frames)\n", o.shot, o.w, o.h, scenario_name(o.scenario),
               o.frames);
        return 0;
    }
    char title[96];
    snprintf(title, sizeof title, "starsim - %s", scenario_name(o.scenario));
    if (pf_open(o.w, o.h, title) != 0) {
        fprintf(stderr, "no display available\n"
                        "hint: run inside a desktop session (on Linux set DISPLAY, or use xvfb-run),\n"
                        "      or render headless with --shot FILE.ppm --frames N\n");
        return 1;
    }
    if (app_init(o.w, o.h, o.seed, 1) != 0 || load_scenario(o.scenario) != 0) {
        fprintf(stderr, "starsim: out of memory\n");
        app_shutdown();
        pf_close();
        return 1;
    }
    run_loop();
    app_shutdown();
    pf_close();
    return 0;
}
