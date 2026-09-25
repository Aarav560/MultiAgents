#include "world.h"

#include <stdlib.h>
#include <string.h>

body body_make(const char *name, body_kind kind, double mass, double radius, vec3 pos, vec3 vel) {
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
    b.acc = vec3_zero();
    b.color.r = b.color.g = b.color.b = 255;
    b.alive = 1;
    b.id = -1;
    return b;
}

const char *body_kind_name(body_kind k) {
    switch (k) {
    case BODY_STAR: return "star";
    case BODY_PLANET: return "planet";
    case BODY_MOON: return "moon";
    case BODY_ASTEROID: return "asteroid";
    case BODY_SPACECRAFT: return "spacecraft";
    case BODY_PARTICLE: return "particle";
    }
    return "unknown";
}

int world_init(world *w, int capacity) {
    memset(w, 0, sizeof *w);
    if (capacity < 4) capacity = 4;
    w->bodies = calloc((size_t)capacity, sizeof(body));
    if (!w->bodies) return -1;
    w->capacity = capacity;
    w->G = G_SI;
    return 0;
}

void world_free(world *w) {
    free(w->bodies);
    memset(w, 0, sizeof *w);
}

int world_add(world *w, const body *b) {
    if (w->count == w->capacity) {
        int cap = w->capacity ? w->capacity * 2 : 4;
        body *nb = realloc(w->bodies, (size_t)cap * sizeof(body));
        if (!nb) return -1;
        w->bodies = nb;
        w->capacity = cap;
    }
    w->bodies[w->count] = *b;
    w->bodies[w->count].id = w->next_id++;
    return w->count++;
}

body *world_find(world *w, const char *name) {
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive && strcmp(w->bodies[i].name, name) == 0) return &w->bodies[i];
    return NULL;
}

int world_compact(world *w) {
    int j = 0;
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) w->bodies[j++] = w->bodies[i];
    int removed = w->count - j;
    w->count = j;
    return removed;
}

int world_alive(const world *w) {
    int n = 0;
    for (int i = 0; i < w->count; i++) n += w->bodies[i].alive != 0;
    return n;
}

double world_total_mass(const world *w) {
    double m = 0.0;
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) m += w->bodies[i].mass;
    return m;
}

vec3 world_com(const world *w) {
    vec3 c = vec3_zero();
    double m = world_total_mass(w);
    if (m <= 0.0) return c;
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) c = vec3_madd(c, w->bodies[i].pos, w->bodies[i].mass);
    return vec3_scale(c, 1.0 / m);
}

vec3 world_momentum(const world *w) {
    vec3 p = vec3_zero();
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive) p = vec3_madd(p, w->bodies[i].vel, w->bodies[i].mass);
    return p;
}

vec3 world_com_vel(const world *w) {
    double m = world_total_mass(w);
    return m > 0.0 ? vec3_scale(world_momentum(w), 1.0 / m) : vec3_zero();
}

void world_recenter(world *w) {
    vec3 c = world_com(w), v = world_com_vel(w);
    for (int i = 0; i < w->count; i++) {
        w->bodies[i].pos = vec3_sub(w->bodies[i].pos, c);
        w->bodies[i].vel = vec3_sub(w->bodies[i].vel, v);
    }
}

double world_extent(const world *w) {
    vec3 c = world_com(w);
    double r = 0.0;
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        double d = vec3_dist(w->bodies[i].pos, c);
        if (d > r) r = d;
    }
    return r;
}
