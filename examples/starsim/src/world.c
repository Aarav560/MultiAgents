#include "sim.h"

#include <stdlib.h>
#include <string.h>

body body_make(const char *name, body_kind kind, double mass, double radius, vec2 pos, vec2 vel,
               uint32_t color) {
    body b;
    memset(&b, 0, sizeof b);
    if (name) {
        strncpy(b.name, name, BODY_NAME_MAX - 1);
        b.name[BODY_NAME_MAX - 1] = '\0';
    }
    b.kind = kind;
    b.mass = mass;
    b.radius = radius;
    b.pos = pos;
    b.vel = vel;
    b.color = color;
    b.alive = 1;
    b.id = -1;
    return b;
}

int world_init(world *w, int cap) {
    memset(w, 0, sizeof *w);
    if (cap < 16) cap = 16;
    w->b = malloc((size_t)cap * sizeof(body));
    if (!w->b) return -1;
    w->cap = cap;
    w->G = 1.0;
    return 0;
}

void world_free(world *w) {
    free(w->b);
    memset(w, 0, sizeof *w);
}

void world_clear(world *w) {
    w->n = 0;
    w->t = 0.0;
}

int world_add(world *w, const body *b) {
    if (w->n == w->cap) {
        int cap = w->cap ? w->cap * 2 : 16;
        body *nb = realloc(w->b, (size_t)cap * sizeof(body));
        if (!nb) return -1;
        w->b = nb;
        w->cap = cap;
    }
    w->b[w->n] = *b;
    w->b[w->n].id = w->next_id++;
    return w->n++;
}

int world_compact(world *w) {
    int j = 0;
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) w->b[j++] = w->b[i];
    int removed = w->n - j;
    w->n = j;
    return removed;
}

int world_index_of(const world *w, int id) {
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive && w->b[i].id == id) return i;
    return -1;
}

int world_nearest(const world *w, vec2 p, double max_dist) {
    int best = -1;
    double best_d2 = max_dist * max_dist;
    for (int i = 0; i < w->n; i++) {
        if (!w->b[i].alive) continue;
        double d2 = v2_len2(v2_sub(w->b[i].pos, p));
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

double world_mass(const world *w) {
    double m = 0.0;
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) m += w->b[i].mass;
    return m;
}

vec2 world_com(const world *w) {
    vec2 c = v2(0.0, 0.0);
    double m = world_mass(w);
    if (m <= 0.0) return c;
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) c = v2_madd(c, w->b[i].pos, w->b[i].mass);
    return v2_scale(c, 1.0 / m);
}

vec2 world_momentum(const world *w) {
    vec2 p = v2(0.0, 0.0);
    for (int i = 0; i < w->n; i++)
        if (w->b[i].alive) p = v2_madd(p, w->b[i].vel, w->b[i].mass);
    return p;
}

void world_recenter(world *w) {
    double m = world_mass(w);
    if (m <= 0.0) return;
    vec2 c = world_com(w), v = v2_scale(world_momentum(w), 1.0 / m);
    for (int i = 0; i < w->n; i++) {
        w->b[i].pos = v2_sub(w->b[i].pos, c);
        w->b[i].vel = v2_sub(w->b[i].vel, v);
    }
}
