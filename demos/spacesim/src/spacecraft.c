#include "spacecraft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void flight_plan_init(flight_plan *p) {
    memset(p, 0, sizeof *p);
}

void flight_plan_free(flight_plan *p) {
    free(p->burns);
    memset(p, 0, sizeof *p);
}

int flight_plan_add(flight_plan *p, double t, int body_id, vec3 dv) {
    if (p->count == p->capacity) {
        int cap = p->capacity ? p->capacity * 2 : 4;
        burn *nb = realloc(p->burns, (size_t)cap * sizeof(burn));
        if (!nb) return -1;
        p->burns = nb;
        p->capacity = cap;
    }
    /* Find the first position with t strictly greater than the new burn's t, so
       equal-t insertions keep insertion order (stable). */
    int i = p->count;
    while (i > 0 && p->burns[i - 1].t > t) i--;
    memmove(&p->burns[i + 1], &p->burns[i], (size_t)(p->count - i) * sizeof(burn));
    p->burns[i].t = t;
    p->burns[i].dv = dv;
    p->burns[i].body_id = body_id;
    p->burns[i].done = 0;
    p->count++;
    return i;
}

static body *find_by_id(world *w, int id) {
    for (int i = 0; i < w->count; i++)
        if (w->bodies[i].alive && w->bodies[i].id == id) return &w->bodies[i];
    return NULL;
}

int flight_plan_apply(flight_plan *p, world *w, double t0, double t1) {
    int applied = 0;
    for (int i = 0; i < p->count; i++) {
        burn *b = &p->burns[i];
        if (b->done) continue;
        if (b->t < t0 || b->t >= t1) continue;
        body *bd = find_by_id(w, b->body_id);
        if (bd) bd->vel = vec3_add(bd->vel, b->dv);
        b->done = 1;
        applied++;
    }
    return applied;
}

vec3 spacecraft_prograde(const world *w, int ship_index, int central_index) {
    vec3 rel = vec3_sub(w->bodies[ship_index].vel, w->bodies[central_index].vel);
    return vec3_norm(rel);
}

int hohmann_plan(double mu, double r1, double r2, hohmann *out) {
    if (mu <= 0.0 || r1 <= 0.0 || r2 <= 0.0) return -1;
    double a = 0.5 * (r1 + r2);
    double v1_circ = sqrt(mu / r1);
    double v2_circ = sqrt(mu / r2);
    double v1_trans = sqrt(mu * (2.0 / r1 - 1.0 / a));
    double v2_trans = sqrt(mu * (2.0 / r2 - 1.0 / a));
    out->dv1 = v1_trans - v1_circ;
    out->dv2 = v2_circ - v2_trans;
    out->tof = M_PI * sqrt(a * a * a / mu);
    out->a_transfer = a;
    return 0;
}

double spacecraft_delta_v_budget(const flight_plan *p) {
    double sum = 0.0;
    for (int i = 0; i < p->count; i++) sum += vec3_len(p->burns[i].dv);
    return sum;
}
