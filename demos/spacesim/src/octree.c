/* octree.c - Barnes-Hut octree: pooled build nodes, flattened preorder walk, bucketed direct sums. */
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

/* Flattened walk node in depth-first preorder: the first child of node i is i + 1 and
 * skip is the index just past its subtree, so the walk needs no stack. */
typedef struct {
    double cx, cy, cz, mass; /* center of mass, total mass */
    double s2;               /* (edge length)^2 for the opening test */
    double bx, by, bz, half; /* cube center and half edge (containment test) */
    int skip;                /* next node after this subtree */
    int first, nbodies;      /* body range in the packed arrays */
    int pad;
} oct_hot;

typedef struct {
    double x, y, z, m;
} oct_pt;

struct octree {
    oct_node *nodes; /* build-time nodes (freed after flattening) */
    int count, capacity;
    int *next;       /* per-body linked list inside a leaf, -1 terminated */
    oct_hot *hot;    /* count nodes in preorder */
    oct_pt *pts;     /* alive bodies in tree order */
    int *idx;        /* world index of each packed body */
    int nb;          /* packed body count */
};

/* Subtrees with at most this many bodies are summed directly once opened. */
#define OCT_BUCKET 8
/* gravity_barnes_hut shares one interaction list among subtrees of at most this many bodies. */
#define OCT_GROUP 64

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

/* Replaces each node's size by the largest edge of the bounding box of its bodies (never
 * larger than the cube), a tighter "size" for the opening test. Bodies of a node are the
 * contiguous range [first, first + nbodies) of the packed array. */
static void body_extent(octree *t) {
    for (int i = 0; i < t->count; i++) {
        oct_hot *h = &t->hot[i];
        const oct_pt *p = &t->pts[h->first];
        double lo[3] = {p->x, p->y, p->z}, hi[3] = {p->x, p->y, p->z};
        for (int j = 1; j < h->nbodies; j++) {
            double q[3] = {p[j].x, p[j].y, p[j].z};
            for (int k = 0; k < 3; k++) {
                lo[k] = fmin(lo[k], q[k]);
                hi[k] = fmax(hi[k], q[k]);
            }
        }
        double e = fmax(hi[0] - lo[0], fmax(hi[1] - lo[1], hi[2] - lo[2]));
        h->s2 = e * e;
    }
}

/* Lays the build tree out in preorder with packed body ranges. Children always have a larger
 * build index than their parent, so one reverse pass yields subtree sizes. */
static int flatten(octree *t, const world *w) {
    int n = t->count;
    int *size = malloc((size_t)n * sizeof *size);
    int *bcount = malloc((size_t)n * sizeof *bcount);
    int *stack = malloc((size_t)n * sizeof *stack);
    t->hot = malloc((size_t)n * sizeof *t->hot);
    t->pts = malloc((size_t)t->nb * sizeof *t->pts);
    t->idx = malloc((size_t)t->nb * sizeof *t->idx);
    int ok = size && bcount && stack && t->hot && t->pts && t->idx;
    if (ok) {
        for (int i = n - 1; i >= 0; i--) {
            const oct_node *b = &t->nodes[i];
            size[i] = 1;
            bcount[i] = b->nbodies;
            for (int k = 0; k < 8; k++) {
                if (b->child[k] < 0) continue;
                size[i] += size[b->child[k]];
                bcount[i] += bcount[b->child[k]];
            }
        }
        int sp = 0, out = 0, boff = 0;
        stack[sp++] = 0;
        while (sp > 0) {
            int bi = stack[--sp];
            const oct_node *b = &t->nodes[bi];
            oct_hot *h = &t->hot[out];
            h->cx = b->com.x;
            h->cy = b->com.y;
            h->cz = b->com.z;
            h->mass = b->mass;
            h->s2 = 4.0 * b->half * b->half;
            h->bx = b->center.x;
            h->by = b->center.y;
            h->bz = b->center.z;
            h->half = b->half;
            h->skip = out + size[bi];
            h->first = boff;
            h->nbodies = bcount[bi];
            h->pad = 0;
            out++;
            for (int j = b->first; j >= 0; j = t->next[j]) {
                const body *p = &w->bodies[j];
                t->pts[boff] = (oct_pt){p->pos.x, p->pos.y, p->pos.z, p->mass};
                t->idx[boff++] = j;
            }
            for (int k = 7; k >= 0; k--)
                if (b->child[k] >= 0) stack[sp++] = b->child[k];
        }
        body_extent(t);
    }
    free(size);
    free(bcount);
    free(stack);
    free(t->nodes);
    t->nodes = NULL;
    free(t->next);
    t->next = NULL;
    return ok ? 0 : -1;
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
    t->hot = NULL;
    t->pts = NULL;
    t->idx = NULL;
    t->nb = alive;
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
    if (flatten(t, w) < 0) {
        octree_free(t);
        return NULL;
    }
    return t;
}

void octree_free(octree *t) {
    if (!t) return;
    free(t->nodes);
    free(t->next);
    free(t->hot);
    free(t->pts);
    free(t->idx);
    free(t);
}

int octree_node_count(const octree *t) { return t ? t->count : 0; }

/* Sum of m d / (|d|^2 + eps^2)^{3/2} over the tree (Plummer softening); the caller scales by G.
 * Opening criterion size/dist < theta, tested as s^2 < theta^2 d^2; a cube containing pos is
 * always opened. An opened subtree with few bodies is summed directly from the packed array. */
static vec3 walk(const octree *t, double px, double py, double pz, int skip, double th2, double eps2) {
    const oct_hot *hot = t->hot;
    const oct_pt *pts = t->pts;
    double ax = 0.0, ay = 0.0, az = 0.0;
    int i = 0, end = t->count;
    while (i < end) {
        const oct_hot *h = &hot[i];
        double dx = h->cx - px, dy = h->cy - py, dz = h->cz - pz;
        double d2 = dx * dx + dy * dy + dz * dz;
        if (h->nbodies > 1 && h->s2 < th2 * d2 &&
            (fabs(px - h->bx) > h->half || fabs(py - h->by) > h->half || fabs(pz - h->bz) > h->half)) {
            double r2 = d2 + eps2;
            double inv = 1.0 / sqrt(r2);
            double s = h->mass * inv * inv * inv;
            ax += dx * s;
            ay += dy * s;
            az += dz * s;
            i = h->skip;
            continue;
        }
        if (h->nbodies > OCT_BUCKET && h->skip != i + 1) { /* big internal node: descend */
            i++;
            continue;
        }
        for (int j = h->first, e = h->first + h->nbodies; j < e; j++) {
            if (t->idx[j] == skip) continue;
            double qx = pts[j].x - px, qy = pts[j].y - py, qz = pts[j].z - pz;
            double r2 = qx * qx + qy * qy + qz * qz + eps2;
            if (r2 <= 0.0) continue;
            double inv = 1.0 / sqrt(r2);
            double s = pts[j].m * inv * inv * inv;
            ax += qx * s;
            ay += qy * s;
            az += qz * s;
        }
        i = h->skip;
    }
    return vec3_make(ax, ay, az);
}

vec3 octree_accel_at(const octree *t, const world *w, vec3 pos, int skip_index, double theta) {
    if (!t || !w || t->count == 0) return vec3_zero();
    vec3 a = walk(t, pos.x, pos.y, pos.z, skip_index, theta * theta, w->softening * w->softening);
    return vec3_scale(a, w->G);
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

/* Growable interaction list of point masses (accepted node monopoles and opened bodies). */
typedef struct {
    oct_pt *p;
    int n, cap;
} ilist;

static int ilist_push(ilist *l, oct_pt q) {
    if (l->n == l->cap) {
        int cap = l->cap ? 2 * l->cap : 1024;
        oct_pt *p = realloc(l->p, (size_t)cap * sizeof *p);
        if (!p) return -1;
        l->p = p;
        l->cap = cap;
    }
    l->p[l->n++] = q;
    return 0;
}

/* Builds the interaction list shared by the bodies [first, first+nb) whose bounding box is
 * lo..hi. A node is accepted only if s^2 < theta^2 dmin^2, dmin being the distance from its
 * center of mass to the box, so every member satisfies its own criterion; a cube that
 * overlaps the box is always opened. */
static int group_list(const octree *t, const double lo[3], const double hi[3], double th2, ilist *l) {
    const oct_hot *hot = t->hot;
    int i = 0, end = t->count;
    l->n = 0;
    while (i < end) {
        const oct_hot *h = &hot[i];
        double c[3] = {h->cx, h->cy, h->cz}, b[3] = {h->bx, h->by, h->bz};
        double d2 = 0.0;
        int overlap = 1;
        for (int k = 0; k < 3; k++) {
            double g = c[k] < lo[k] ? lo[k] - c[k] : (c[k] > hi[k] ? c[k] - hi[k] : 0.0);
            d2 += g * g;
            overlap &= b[k] - h->half <= hi[k] && b[k] + h->half >= lo[k];
        }
        if (h->nbodies > 1 && !overlap && h->s2 < th2 * d2) {
            if (ilist_push(l, (oct_pt){h->cx, h->cy, h->cz, h->mass}) < 0) return -1;
            i = h->skip;
            continue;
        }
        if (h->nbodies > OCT_BUCKET && h->skip != i + 1) {
            i++;
            continue;
        }
        for (int j = h->first, e = h->first + h->nbodies; j < e; j++)
            if (ilist_push(l, t->pts[j]) < 0) return -1;
        i = h->skip;
    }
    return 0;
}

/* Softened sum over the list; the body itself (and any coincident one) has d = 0 and adds 0.
 * Entries go in pairs sharing one division: with x = r_a^3, y = r_b^3 and q = 1/(x y),
 * 1/r_a^3 = y q and 1/r_b^3 = x q. A pair whose product leaves the safe range falls back. */
static vec3 list_accel(const ilist *l, const oct_pt *p, double eps2) {
    double ax = 0.0, ay = 0.0, az = 0.0;
    double px = p->x, py = p->y, pz = p->z;
    int j = 0;
    for (; j + 1 < l->n; j += 2) {
        const oct_pt *qa = &l->p[j], *qb = &l->p[j + 1];
        double dxa = qa->x - px, dya = qa->y - py, dza = qa->z - pz;
        double dxb = qb->x - px, dyb = qb->y - py, dzb = qb->z - pz;
        double ra = dxa * dxa + dya * dya + dza * dza + eps2;
        double rb = dxb * dxb + dyb * dyb + dzb * dzb + eps2;
        double x = ra * sqrt(ra), y = rb * sqrt(rb), xy = x * y;
        double sa, sb;
        if (xy > 1e-280 && xy < 1e280) {
            double q = 1.0 / xy;
            sa = qa->m * y * q;
            sb = qb->m * x * q;
        } else {
            sa = x > 0.0 ? qa->m / x : 0.0;
            sb = y > 0.0 ? qb->m / y : 0.0;
        }
        ax += dxa * sa + dxb * sb;
        ay += dya * sa + dyb * sb;
        az += dza * sa + dzb * sb;
    }
    if (j < l->n) {
        const oct_pt *q = &l->p[j];
        double dx = q->x - px, dy = q->y - py, dz = q->z - pz;
        double r2 = dx * dx + dy * dy + dz * dz + eps2;
        if (r2 > 0.0) {
            double s = q->m / (r2 * sqrt(r2));
            ax += dx * s;
            ay += dy * s;
            az += dz * s;
        }
    }
    return vec3_make(ax, ay, az);
}

/* Walks the tree once per group (a node of at most OCT_GROUP bodies, or a depth-capped leaf). */
static int group_walk(const octree *t, world *w, double th2, double eps2) {
    ilist l = {NULL, 0, 0};
    int i = 0;
    while (i < t->count) {
        const oct_hot *h = &t->hot[i];
        if (h->nbodies > OCT_GROUP && h->skip != i + 1) {
            i++;
            continue;
        }
        double lo[3], hi[3];
        const oct_pt *p = &t->pts[h->first];
        lo[0] = hi[0] = p->x;
        lo[1] = hi[1] = p->y;
        lo[2] = hi[2] = p->z;
        for (int j = 1; j < h->nbodies; j++) {
            double q[3] = {p[j].x, p[j].y, p[j].z};
            for (int k = 0; k < 3; k++) {
                lo[k] = fmin(lo[k], q[k]);
                hi[k] = fmax(hi[k], q[k]);
            }
        }
        if (group_list(t, lo, hi, th2, &l) < 0) {
            free(l.p);
            return -1;
        }
        for (int j = h->first, e = h->first + h->nbodies; j < e; j++)
            w->bodies[t->idx[j]].acc = vec3_scale(list_accel(&l, &t->pts[j], eps2), w->G);
        i = h->skip;
    }
    free(l.p);
    return 0;
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
    double th2 = theta * theta, eps2 = w->softening * w->softening;
    if (group_walk(t, w, th2, eps2) < 0) {
        /* List allocation failed: per-body walks need no extra memory. */
        for (int j = 0; j < t->nb; j++) {
            const oct_pt *p = &t->pts[j];
            int bi = t->idx[j];
            w->bodies[bi].acc = vec3_scale(walk(t, p->x, p->y, p->z, bi, th2, eps2), w->G);
        }
    }
    octree_free(t);
}
