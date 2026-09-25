/* quadtree.c - Barnes-Hut gravity on a 2D quadtree.
 *
 * Alive bodies are copied into a packed array that the build partitions in place, so every node
 * owns a contiguous range of it and leaves (up to LEAF_MAX bodies) are scanned linearly. Nodes live
 * in a pool reused across calls; children of a node are allocated consecutively. The force walk is
 * iterative with an explicit stack. Pairs at exactly zero distance exert no force. */
#include "sim.h"

#include <stdlib.h>

#define LEAF_MAX 8
#define MAX_DEPTH 48
#define STACK_MAX (MAX_DEPTH * 4 + 8)

typedef struct {
    double x, y, m;
    int idx;                /* index into w->b */
} pbody;

typedef struct {
    double cx, cy, m;       /* centre of mass and total mass */
    double bx, by, h;       /* box centre and half width */
    double s2;              /* (box width)^2, for the opening test */
    int first, count;       /* leaf: body range; internal: first child node, child count */
    int leaf;
} qnode;

static pbody *g_bodies;
static int g_bodies_cap;
static qnode *g_nodes;
static int g_nodes_cap;
static int g_nodes_n;

static int ensure_bodies(int n) {
    if (n <= g_bodies_cap) return 0;
    int cap = g_bodies_cap ? g_bodies_cap : 256;
    while (cap < n) cap *= 2;
    pbody *nb = realloc(g_bodies, (size_t)cap * sizeof *nb);
    if (!nb) return -1;
    g_bodies = nb;
    g_bodies_cap = cap;
    return 0;
}

/* Reserves k consecutive nodes; returns the first index or -1. */
static int alloc_nodes(int k) {
    if (g_nodes_n + k > g_nodes_cap) {
        int cap = g_nodes_cap ? g_nodes_cap : 1024;
        while (cap < g_nodes_n + k) cap *= 2;
        qnode *nn = realloc(g_nodes, (size_t)cap * sizeof *nn);
        if (!nn) return -1;
        g_nodes = nn;
        g_nodes_cap = cap;
    }
    int first = g_nodes_n;
    g_nodes_n += k;
    return first;
}

static void swap_pb(pbody *a, pbody *b) {
    pbody t = *a;
    *a = *b;
    *b = t;
}

/* Moves bodies with (y < v) or (x < v) to the front of [lo, hi); returns the split point. */
static int partition(int lo, int hi, int axis_y, double v) {
    int i = lo, j = hi - 1;
    while (i <= j) {
        double ci = axis_y ? g_bodies[i].y : g_bodies[i].x;
        if (ci < v) {
            i++;
        } else {
            swap_pb(&g_bodies[i], &g_bodies[j]);
            j--;
        }
    }
    return i;
}

static void make_leaf(int ni, int lo, int hi) {
    double m = 0.0, sx = 0.0, sy = 0.0;
    for (int k = lo; k < hi; k++) {
        m += g_bodies[k].m;
        sx += g_bodies[k].x * g_bodies[k].m;
        sy += g_bodies[k].y * g_bodies[k].m;
    }
    qnode *nd = &g_nodes[ni];
    nd->leaf = 1;
    nd->first = lo;
    nd->count = hi - lo;
    nd->m = m;
    nd->cx = m > 0.0 ? sx / m : nd->bx;
    nd->cy = m > 0.0 ? sy / m : nd->by;
}

/* Builds node ni over bodies [lo, hi). Returns 0 or -1 on allocation failure. */
static int build(int ni, int lo, int hi, int depth) {
    if (hi - lo <= LEAF_MAX || depth >= MAX_DEPTH) {
        make_leaf(ni, lo, hi);
        return 0;
    }
    double bx = g_nodes[ni].bx, by = g_nodes[ni].by, h = g_nodes[ni].h;
    /* quadrant order: (-x,-y) (+x,-y) (-x,+y) (+x,+y) */
    int split_y = partition(lo, hi, 1, by);
    int bounds[5] = {lo, partition(lo, split_y, 0, bx), split_y, partition(split_y, hi, 0, bx), hi};
    int k = 0;
    for (int q = 0; q < 4; q++)
        if (bounds[q + 1] > bounds[q]) k++;
    int first = alloc_nodes(k);
    if (first < 0) return -1;
    double hh = 0.5 * h;
    int c = first;
    for (int q = 0; q < 4; q++) {
        if (bounds[q + 1] == bounds[q]) continue;
        qnode *ch = &g_nodes[c];
        ch->bx = bx + ((q & 1) ? hh : -hh);
        ch->by = by + ((q & 2) ? hh : -hh);
        ch->h = hh;
        ch->s2 = 4.0 * hh * hh;
        if (build(c, bounds[q], bounds[q + 1], depth + 1) != 0) return -1;
        c++;
    }
    double m = 0.0, sx = 0.0, sy = 0.0;
    for (c = first; c < first + k; c++) {
        m += g_nodes[c].m;
        sx += g_nodes[c].cx * g_nodes[c].m;
        sy += g_nodes[c].cy * g_nodes[c].m;
    }
    qnode *nd = &g_nodes[ni];
    nd->leaf = 0;
    nd->first = first;
    nd->count = k;
    nd->m = m;
    nd->cx = m > 0.0 ? sx / m : bx;
    nd->cy = m > 0.0 ? sy / m : by;
    return 0;
}

/* Fallback when the pool cannot grow: plain O(n^2) sum with the same softening. */
static void direct_sum(world *w) {
    double eps2 = w->soft * w->soft;
    for (int i = 0; i < w->n; i++) w->b[i].acc = v2(0.0, 0.0);
    for (int i = 0; i < w->n; i++) {
        body *bi = &w->b[i];
        if (!bi->alive) continue;
        double ax = 0.0, ay = 0.0;
        for (int j = 0; j < w->n; j++) {
            const body *bj = &w->b[j];
            if (j == i || !bj->alive) continue;
            double dx = bj->pos.x - bi->pos.x, dy = bj->pos.y - bi->pos.y;
            double r2 = dx * dx + dy * dy;
            if (r2 == 0.0) continue;
            r2 += eps2;
            double inv = 1.0 / sqrt(r2);
            double f = bj->mass * inv * inv * inv;
            ax += f * dx;
            ay += f * dy;
        }
        bi->acc = v2(w->G * ax, w->G * ay);
    }
}

static void walk(int k, double theta2, double eps2, double G, vec2 *out) {
    const double px = g_bodies[k].x, py = g_bodies[k].y;
    double ax = 0.0, ay = 0.0;
    int stack[STACK_MAX];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const qnode *nd = &g_nodes[stack[--sp]];
        if (nd->m == 0.0) continue;
        if (nd->leaf) {
            for (int j = nd->first; j < nd->first + nd->count; j++) {
                if (j == k) continue;
                double dx = g_bodies[j].x - px, dy = g_bodies[j].y - py;
                double r2 = dx * dx + dy * dy;
                if (r2 == 0.0) continue;
                r2 += eps2;
                double inv = 1.0 / sqrt(r2);
                double f = g_bodies[j].m * inv * inv * inv;
                ax += f * dx;
                ay += f * dy;
            }
            continue;
        }
        double dx = nd->cx - px, dy = nd->cy - py;
        double d2 = dx * dx + dy * dy;
        /* Never approximate a node containing the body itself (would add self-force). */
        int inside = fabs(px - nd->bx) <= nd->h && fabs(py - nd->by) <= nd->h;
        if (!inside && nd->s2 < theta2 * d2) {
            double r2 = d2 + eps2;
            double inv = 1.0 / sqrt(r2);
            double f = nd->m * inv * inv * inv;
            ax += f * dx;
            ay += f * dy;
        } else {
            for (int c = nd->first; c < nd->first + nd->count; c++) stack[sp++] = c;
        }
    }
    *out = v2(G * ax, G * ay);
}

void gravity_bh(world *w, double theta) {
    int n = 0;
    for (int i = 0; i < w->n; i++) {
        w->b[i].acc = v2(0.0, 0.0);
        if (w->b[i].alive) n++;
    }
    if (n < 2) return;
    if (ensure_bodies(n) != 0) {
        direct_sum(w);
        return;
    }
    double minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
    n = 0;
    for (int i = 0; i < w->n; i++) {
        const body *b = &w->b[i];
        if (!b->alive) continue;
        pbody *p = &g_bodies[n++];
        p->x = b->pos.x;
        p->y = b->pos.y;
        p->m = b->mass;
        p->idx = i;
        if (p->x < minx) minx = p->x;
        if (p->x > maxx) maxx = p->x;
        if (p->y < miny) miny = p->y;
        if (p->y > maxy) maxy = p->y;
    }
    double h = 0.5 * fmax(maxx - minx, maxy - miny);
    h = h * (1.0 + 1e-9) + 1e-300;
    if (!isfinite(h)) h = 1e300;

    g_nodes_n = 0;
    int root = alloc_nodes(1);
    if (root < 0) {
        direct_sum(w);
        return;
    }
    g_nodes[root].bx = 0.5 * (minx + maxx);
    g_nodes[root].by = 0.5 * (miny + maxy);
    g_nodes[root].h = h;
    g_nodes[root].s2 = 4.0 * h * h;
    if (build(root, 0, n, 0) != 0) {
        direct_sum(w);
        return;
    }

    double theta2 = theta > 0.0 ? theta * theta : 0.0;
    double eps2 = w->soft * w->soft;
    for (int k = 0; k < n; k++) walk(k, theta2, eps2, w->G, &w->b[g_bodies[k].idx].acc);
}
