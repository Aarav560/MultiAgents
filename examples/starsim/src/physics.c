#include "sim.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

/* ---- gravity ------------------------------------------------------------ */

void gravity_direct(world *w) {
    body *b = w->b;
    int n = w->n;
    double G = w->G, s2 = w->soft * w->soft;
    for (int i = 0; i < n; i++) b[i].acc = v2(0.0, 0.0);
    /* Pairwise loop: equal and opposite forces keep momentum conserved to rounding. */
    for (int i = 0; i < n; i++) {
        if (!b[i].alive) continue;
        vec2 pi = b[i].pos, ai = b[i].acc;
        double mi = b[i].mass;
        for (int j = i + 1; j < n; j++) {
            if (!b[j].alive) continue;
            vec2 d = v2_sub(b[j].pos, pi);
            double r2 = v2_len2(d) + s2;
            if (r2 <= 0.0) continue;
            double inv = G / (r2 * sqrt(r2));
            ai = v2_madd(ai, d, b[j].mass * inv);
            b[j].acc = v2_madd(b[j].acc, d, -mi * inv);
        }
        b[i].acc = ai;
    }
}

void gravity_compute(world *w, gravity_mode mode, double theta) {
    if (mode == GRAV_BH)
        gravity_bh(w, theta);
    else
        gravity_direct(w);
}

static void kick(world *w, double h) {
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) w->b[i].vel = v2_madd(w->b[i].vel, w->b[i].acc, h);
}

static void drift(world *w, double h) {
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) w->b[i].pos = v2_madd(w->b[i].pos, w->b[i].vel, h);
}

void sim_step(world *w, double dt, gravity_mode mode, double theta) {
    gravity_compute(w, mode, theta);
    kick(w, 0.5 * dt);
    drift(w, dt);
    gravity_compute(w, mode, theta);
    kick(w, 0.5 * dt);
    w->t += dt;
}

double world_energy(const world *w) {
    const body *b = w->b;
    double ke = 0.0, pe = 0.0, s2 = w->soft * w->soft;
    for (int i = 0; i < w->n; i++) {
        if (!b[i].alive) continue;
        ke += 0.5 * b[i].mass * v2_len2(b[i].vel);
        double row = 0.0;
        for (int j = i + 1; j < w->n; j++) {
            if (!b[j].alive) continue;
            double r2 = v2_len2(v2_sub(b[j].pos, b[i].pos)) + s2;
            if (r2 > 0.0) row += b[j].mass / sqrt(r2);
        }
        pe -= w->G * b[i].mass * row;
    }
    return ke + pe;
}

/* ---- collisions --------------------------------------------------------- */

/* Merges bodies i and j (both alive); the heavier (ties: i) survives. Returns survivor index. */
static int merge_pair(world *w, int i, int j) {
    body *b = w->b;
    int keep = b[j].mass > b[i].mass ? j : i, gone = keep == i ? j : i;
    body *k = &b[keep], *g = &b[gone];
    double m = k->mass + g->mass;
    if (m > 0.0) {
        k->pos = v2_scale(v2_add(v2_scale(k->pos, k->mass), v2_scale(g->pos, g->mass)), 1.0 / m);
        k->vel = v2_scale(v2_add(v2_scale(k->vel, k->mass), v2_scale(g->vel, g->mass)), 1.0 / m);
    }
    k->mass = m;
    k->radius = sqrt(k->radius * k->radius + g->radius * g->radius);
    g->alive = 0;
    g->mass = 0.0;
    return keep;
}

static int overlaps(const body *a, const body *c) {
    double r = a->radius + c->radius;
    return v2_len2(v2_sub(a->pos, c->pos)) < r * r;
}

/* Both passes use the same rule so they merge identically: bodies are visited in index order,
   each untouched body i merges with the lowest-index untouched overlapping j > i, and a body takes
   part in at most one merge per pass (so the grid, built from the pass's start state, stays valid). */
static int brute_pass(world *w, unsigned char *touched) {
    int merges = 0;
    for (int i = 0; i < w->n; i++) {
        if (!w->b[i].alive || touched[i]) continue;
        for (int j = i + 1; j < w->n; j++) {
            if (!w->b[j].alive || touched[j] || !overlaps(&w->b[i], &w->b[j])) continue;
            merge_pair(w, i, j);
            touched[i] = touched[j] = 1;
            merges++;
            break;
        }
    }
    return merges;
}

typedef struct {
    int *start;     /* tsize + 1 bucket offsets */
    int *items;     /* body indices sorted by bucket */
    uint32_t *key;  /* bucket of each body */
    int64_t *cx, *cy;
    uint32_t mask;
    double cell, ox, oy;
} grid;

static uint32_t cell_hash(int64_t x, int64_t y, uint32_t mask) {
    uint64_t h = (uint64_t)x * 0x9E3779B97F4A7C15ull ^ (uint64_t)y * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    return (uint32_t)h & mask;
}

static int64_t cell_coord(double v, double origin, double cell) {
    double c = floor((v - origin) / cell);
    if (c > 4e18) c = 4e18;
    if (c < -4e18) c = -4e18;
    return (int64_t)c;
}

static void grid_build(grid *g, const world *w) {
    uint32_t tsize = g->mask + 1;
    for (uint32_t t = 0; t <= tsize; t++) g->start[t] = 0;
    for (int i = 0; i < w->n; i++) {
        if (!w->b[i].alive) continue;
        g->cx[i] = cell_coord(w->b[i].pos.x, g->ox, g->cell);
        g->cy[i] = cell_coord(w->b[i].pos.y, g->oy, g->cell);
        g->key[i] = cell_hash(g->cx[i], g->cy[i], g->mask);
        g->start[g->key[i] + 1]++;
    }
    for (uint32_t t = 0; t < tsize; t++) g->start[t + 1] += g->start[t];
    /* counting sort; start[] is shifted back afterwards */
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) g->items[g->start[g->key[i]]++] = i;
    for (uint32_t t = tsize; t > 0; t--) g->start[t] = g->start[t - 1];
    g->start[0] = 0;
}

static int grid_find(const grid *g, const world *w, const unsigned char *touched, int i) {
    int best = -1;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            uint32_t k = cell_hash(g->cx[i] + dx, g->cy[i] + dy, g->mask);
            for (int s = g->start[k]; s < g->start[k + 1]; s++) {
                int j = g->items[s];
                if (j <= i || (best >= 0 && j >= best) || touched[j] || !w->b[j].alive) continue;
                if (overlaps(&w->b[i], &w->b[j])) best = j;
            }
        }
    }
    return best;
}

static int grid_pass(world *w, grid *g, unsigned char *touched) {
    double maxr = 0.0, minx = HUGE_VAL, miny = HUGE_VAL;
    for (int i = 0; i < w->n; i++) {
        if (!w->b[i].alive) continue;
        if (w->b[i].radius > maxr) maxr = w->b[i].radius;
        if (w->b[i].pos.x < minx) minx = w->b[i].pos.x;
        if (w->b[i].pos.y < miny) miny = w->b[i].pos.y;
    }
    if (maxr <= 0.0) return 0;
    g->cell = 2.0 * maxr;
    g->ox = minx;
    g->oy = miny;
    grid_build(g, w);
    int merges = 0;
    for (int i = 0; i < w->n; i++) {
        if (!w->b[i].alive || touched[i]) continue;
        int j = grid_find(g, w, touched, i);
        if (j < 0) continue;
        merge_pair(w, i, j);
        touched[i] = touched[j] = 1;
        merges++;
    }
    return merges;
}

int collide_merge(world *w) {
    int n = w->n, total = 0, merges;
    if (n < 2) return 0;
    unsigned char *touched = malloc((size_t)n);
    if (!touched) return 0;
    grid g = {0};
    int use_grid = 0;
    if (n > 64) {
        uint32_t tsize = 64;
        while (tsize < (uint32_t)n * 2u) tsize <<= 1;
        g.mask = tsize - 1;
        g.start = malloc(((size_t)tsize + 1) * sizeof *g.start);
        g.items = malloc((size_t)n * sizeof *g.items);
        g.key = malloc((size_t)n * sizeof *g.key);
        g.cx = malloc((size_t)n * sizeof *g.cx);
        g.cy = malloc((size_t)n * sizeof *g.cy);
        use_grid = g.start && g.items && g.key && g.cx && g.cy;
    }
    do {
        for (int i = 0; i < n; i++) touched[i] = 0;
        merges = use_grid ? grid_pass(w, &g, touched) : brute_pass(w, touched);
        total += merges;
    } while (merges > 0);
    free(g.start);
    free(g.items);
    free(g.key);
    free(g.cx);
    free(g.cy);
    free(touched);
    return total;
}

/* ---- path prediction ---------------------------------------------------- */

/* Acceleration of a test particle at p; returns 1 if p lies inside a body. */
static int test_accel(const world *w, vec2 p, vec2 *acc) {
    double s2 = w->soft * w->soft;
    vec2 a = v2(0.0, 0.0);
    int hit = 0;
    for (int j = 0; j < w->n; j++) {
        const body *b = &w->b[j];
        if (!b->alive) continue;
        vec2 d = v2_sub(b->pos, p);
        double d2 = v2_len2(d);
        if (d2 < b->radius * b->radius) hit = 1;
        double r2 = d2 + s2;
        if (r2 > 0.0) a = v2_madd(a, d, w->G * b->mass / (r2 * sqrt(r2)));
    }
    *acc = a;
    return hit;
}

int predict_path(const world *w, vec2 pos, vec2 vel, double dt, int max_pts, vec2 *out) {
    if (max_pts <= 0 || !out) return 0;
    vec2 a;
    out[0] = pos;
    if (test_accel(w, pos, &a)) return 1;
    int k = 1;
    while (k < max_pts) {
        vel = v2_madd(vel, a, 0.5 * dt);
        pos = v2_madd(pos, vel, dt);
        int hit = test_accel(w, pos, &a);
        vel = v2_madd(vel, a, 0.5 * dt);
        out[k++] = pos;
        if (hit) break;
    }
    return k;
}
