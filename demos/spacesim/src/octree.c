/* octree.c - Barnes-Hut octree with pooled nodes and multi-body leaves at the depth cap. */
#include "octree.h"

#include <math.h>
#include <stdlib.h>

#define OCT_MAX_DEPTH 64

typedef struct {
    vec3 center;   /* cube center */
    double half;   /* half edge length */
    vec3 com;      /* center of mass (mass-weighted sum during build) */
    double mass;
    int child[8];  /* node indices, -1 = none */
    int first;     /* leaf: first body index in the list, -1 = none */
    int nbodies;   /* bodies in the leaf list */
    int leaf;      /* 1 = no children */
} oct_node;

struct octree {
    oct_node *nodes;
    int count, capacity;
    int *next; /* per-body linked list inside a leaf, -1 terminated */
};

static int node_new(octree *t, vec3 center, double half) {
    if (t->count == t->capacity) {
        int cap = t->capacity * 2;
        oct_node *n = realloc(t->nodes, (size_t)cap * sizeof *n);
        if (!n) return -1;
        t->nodes = n;
        t->capacity = cap;
    }
    oct_node *n = &t->nodes[t->count];
    n->center = center;
    n->half = half;
    n->com = vec3_zero();
    n->mass = 0.0;
    for (int k = 0; k < 8; k++) n->child[k] = -1;
    n->first = -1;
    n->nbodies = 0;
    n->leaf = 1;
    return t->count++;
}

static int octant_of(vec3 c, vec3 p) {
    return (p.x >= c.x ? 1 : 0) | (p.y >= c.y ? 2 : 0) | (p.z >= c.z ? 4 : 0);
}

/* Returns the child of node ni for octant k, creating it if needed (-1 on OOM). */
static int child_get(octree *t, int ni, int k) {
    if (t->nodes[ni].child[k] >= 0) return t->nodes[ni].child[k];
    double h = t->nodes[ni].half * 0.5;
    vec3 c = t->nodes[ni].center;
    c.x += (k & 1) ? h : -h;
    c.y += (k & 2) ? h : -h;
    c.z += (k & 4) ? h : -h;
    int ci = node_new(t, c, h);
    if (ci < 0) return -1;
    t->nodes[ni].child[k] = ci;
    return ci;
}

static void leaf_push(octree *t, int ni, int bi) {
    t->next[bi] = t->nodes[ni].first;
    t->nodes[ni].first = bi;
    t->nodes[ni].nbodies++;
}

/* Inserts body bi below root; mass and mass-weighted position accumulate along the path. */
static int tree_insert(octree *t, const world *w, int bi) {
    const body *b = &w->bodies[bi];
    int ni = 0;
    for (int depth = 0;; depth++) {
        oct_node *n = &t->nodes[ni];
        n->mass += b->mass;
        n->com = vec3_madd(n->com, b->pos, b->mass);
        if (n->leaf) {
            if (n->nbodies == 0 || depth >= OCT_MAX_DEPTH) {
                leaf_push(t, ni, bi);
                return 0;
            }
            /* Subdivide: push the single resident body one level down. */
            int old = n->first;
            int k = octant_of(n->center, w->bodies[old].pos);
            int ci = child_get(t, ni, k);
            if (ci < 0) return -1;
            n = &t->nodes[ni];
            n->first = -1;
            n->nbodies = 0;
            n->leaf = 0;
            oct_node *c = &t->nodes[ci];
            c->mass += w->bodies[old].mass;
            c->com = vec3_madd(c->com, w->bodies[old].pos, w->bodies[old].mass);
            leaf_push(t, ci, old);
        }
        int ci = child_get(t, ni, octant_of(t->nodes[ni].center, b->pos));
        if (ci < 0) return -1;
        ni = ci;
    }
}

static void finalize_com(octree *t) {
    for (int i = 0; i < t->count; i++) {
        oct_node *n = &t->nodes[i];
        n->com = n->mass != 0.0 ? vec3_scale(n->com, 1.0 / n->mass) : n->center;
    }
}

octree *octree_build(const world *w) {
    if (!w || w->count <= 0) return NULL;
    vec3 lo = vec3_zero(), hi = vec3_zero();
    int alive = 0;
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) continue;
        vec3 p = w->bodies[i].pos;
        if (!alive) {
            lo = hi = p;
        } else {
            lo = vec3_make(fmin(lo.x, p.x), fmin(lo.y, p.y), fmin(lo.z, p.z));
            hi = vec3_make(fmax(hi.x, p.x), fmax(hi.y, p.y), fmax(hi.z, p.z));
        }
        alive++;
    }
    if (!alive) return NULL;
    octree *t = malloc(sizeof *t);
    if (!t) return NULL;
    t->count = 0;
    t->capacity = 2 * alive + 8;
    t->nodes = malloc((size_t)t->capacity * sizeof *t->nodes);
    t->next = malloc((size_t)w->count * sizeof *t->next);
    if (!t->nodes || !t->next) {
        octree_free(t);
        return NULL;
    }
    vec3 center = vec3_scale(vec3_add(lo, hi), 0.5);
    double half = 0.5 * fmax(hi.x - lo.x, fmax(hi.y - lo.y, hi.z - lo.z));
    half = half > 0.0 ? half * (1.0 + 1e-9) : 1.0;
    if (node_new(t, center, half) < 0) {
        octree_free(t);
        return NULL;
    }
    for (int i = 0; i < w->count; i++) {
        if (w->bodies[i].alive && tree_insert(t, w, i) < 0) {
            octree_free(t);
            return NULL;
        }
    }
    finalize_com(t);
    return t;
}

void octree_free(octree *t) {
    if (!t) return;
    free(t->nodes);
    free(t->next);
    free(t);
}

int octree_node_count(const octree *t) { return t ? t->count : 0; }

/* Plummer-softened point-mass pull: G m d / (|d|^2 + eps^2)^{3/2}. */
static vec3 pull(vec3 acc, vec3 d, double gm, double eps2) {
    double r2 = vec3_len2(d) + eps2;
    if (r2 <= 0.0) return acc;
    double inv = 1.0 / sqrt(r2);
    return vec3_madd(acc, d, gm * inv * inv * inv);
}

static int inside(const oct_node *n, vec3 p) {
    return fabs(p.x - n->center.x) <= n->half && fabs(p.y - n->center.y) <= n->half &&
           fabs(p.z - n->center.z) <= n->half;
}

static vec3 walk(const octree *t, const world *w, int ni, vec3 pos, int skip, double theta, vec3 acc) {
    const oct_node *n = &t->nodes[ni];
    double eps2 = w->softening * w->softening;
    if (n->leaf) {
        for (int bi = n->first; bi >= 0; bi = t->next[bi]) {
            if (bi == skip) continue;
            const body *b = &w->bodies[bi];
            acc = pull(acc, vec3_sub(b->pos, pos), w->G * b->mass, eps2);
        }
        return acc;
    }
    vec3 d = vec3_sub(n->com, pos);
    double dist = vec3_len(d);
    /* Opening criterion size/dist < theta; a node containing pos is always opened. */
    if (dist > 0.0 && 2.0 * n->half < theta * dist && !inside(n, pos))
        return pull(acc, d, w->G * n->mass, eps2);
    for (int k = 0; k < 8; k++)
        if (n->child[k] >= 0) acc = walk(t, w, n->child[k], pos, skip, theta, acc);
    return acc;
}

vec3 octree_accel_at(const octree *t, const world *w, vec3 pos, int skip_index, double theta) {
    if (!t || !w || t->count == 0) return vec3_zero();
    return walk(t, w, 0, pos, skip_index, theta, vec3_zero());
}

/* Direct-sum fallback used only if the tree cannot be allocated. */
static void direct_fallback(world *w) {
    double eps2 = w->softening * w->softening;
    for (int i = 0; i < w->count; i++) {
        body *bi = &w->bodies[i];
        if (!bi->alive) continue;
        for (int j = i + 1; j < w->count; j++) {
            body *bj = &w->bodies[j];
            if (!bj->alive) continue;
            vec3 d = vec3_sub(bj->pos, bi->pos);
            double r2 = vec3_len2(d) + eps2;
            if (r2 <= 0.0) continue;
            double inv = 1.0 / sqrt(r2);
            double s = w->G * inv * inv * inv;
            bi->acc = vec3_madd(bi->acc, d, s * bj->mass);
            bj->acc = vec3_madd(bj->acc, d, -s * bi->mass);
        }
    }
}

void gravity_barnes_hut(world *w, void *ctx) {
    if (!w) return;
    double theta = ctx ? ((const bh_params *)ctx)->theta : 0.5;
    for (int i = 0; i < w->count; i++) w->bodies[i].acc = vec3_zero();
    octree *t = octree_build(w);
    if (!t) {
        direct_fallback(w);
        return;
    }
    for (int i = 0; i < w->count; i++) {
        body *b = &w->bodies[i];
        if (b->alive) b->acc = octree_accel_at(t, w, b->pos, i, theta);
    }
    octree_free(t);
}
