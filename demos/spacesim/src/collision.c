/* collision.c - perfectly inelastic merging of overlapping bodies. */
#include "collision.h"

#include <math.h>
#include <stdlib.h>

#define COLLISION_GRID_THRESHOLD 64

/* A candidate pair of world indices (lo < hi) to check for overlap. */
typedef struct {
    int a, b;
} coll_pair;

typedef struct {
    coll_pair *items;
    int count, capacity;
} pair_list;

static int pair_list_push(pair_list *pl, int a, int b) {
    if (pl->count == pl->capacity) {
        int newcap = pl->capacity ? pl->capacity * 2 : 16;
        coll_pair *np = realloc(pl->items, (size_t)newcap * sizeof(coll_pair));
        if (!np) return -1;
        pl->items = np;
        pl->capacity = newcap;
    }
    pl->items[pl->count].a = a < b ? a : b;
    pl->items[pl->count].b = a < b ? b : a;
    pl->count++;
    return 0;
}

/* Brute-force O(m^2) candidate generation over the alive indices idx[0..m). */
static int collect_pairs_brute(const world *w, const int *idx, int m, pair_list *pl) {
    (void)w;
    for (int a = 0; a < m; a++) {
        for (int b = a + 1; b < m; b++) {
            if (pair_list_push(pl, idx[a], idx[b]) != 0) return -1;
        }
    }
    return 0;
}

/* Spatial hash grid: cell size = 2 * max radius among alive bodies. Chained
   buckets over the m alive bodies (array-of-slots hashing, singly-linked via
   a `next` array), so no per-cell malloc. */
static int collect_pairs_grid(const world *w, const int *idx, int m, pair_list *pl) {
    double maxr = 0.0;
    for (int k = 0; k < m; k++) {
        double r = w->bodies[idx[k]].radius;
        if (r > maxr) maxr = r;
    }
    double cell = 2.0 * maxr;
    if (!(cell > 0.0)) cell = 1.0;

    long long *cx = malloc((size_t)m * sizeof(long long));
    long long *cy = malloc((size_t)m * sizeof(long long));
    long long *cz = malloc((size_t)m * sizeof(long long));
    if (!cx || !cy || !cz) {
        free(cx);
        free(cy);
        free(cz);
        return -1;
    }
    for (int k = 0; k < m; k++) {
        vec3 p = w->bodies[idx[k]].pos;
        cx[k] = (long long)floor(p.x / cell);
        cy[k] = (long long)floor(p.y / cell);
        cz[k] = (long long)floor(p.z / cell);
    }

    unsigned nbuckets = 16;
    while ((int)nbuckets < m * 2) nbuckets *= 2;

    int *head = malloc((size_t)nbuckets * sizeof(int));
    int *next = malloc((size_t)m * sizeof(int));
    if (!head || !next) {
        free(cx);
        free(cy);
        free(cz);
        free(head);
        free(next);
        return -1;
    }
    for (unsigned b = 0; b < nbuckets; b++) head[b] = -1;

    for (int k = 0; k < m; k++) {
        unsigned long long ux = (unsigned long long)cx[k] * 73856093ULL;
        unsigned long long uy = (unsigned long long)cy[k] * 19349663ULL;
        unsigned long long uz = (unsigned long long)cz[k] * 83492791ULL;
        unsigned h = (unsigned)((ux ^ uy ^ uz) & (nbuckets - 1));
        next[k] = head[h];
        head[h] = k;
    }

    int rc = 0;
    for (int k = 0; k < m && rc == 0; k++) {
        for (int dz = -1; dz <= 1 && rc == 0; dz++) {
            for (int dy = -1; dy <= 1 && rc == 0; dy++) {
                for (int dx = -1; dx <= 1 && rc == 0; dx++) {
                    long long ncx = cx[k] + dx, ncy = cy[k] + dy, ncz = cz[k] + dz;
                    unsigned long long ux = (unsigned long long)ncx * 73856093ULL;
                    unsigned long long uy = (unsigned long long)ncy * 19349663ULL;
                    unsigned long long uz = (unsigned long long)ncz * 83492791ULL;
                    unsigned h = (unsigned)((ux ^ uy ^ uz) & (nbuckets - 1));
                    for (int k2 = head[h]; k2 != -1; k2 = next[k2]) {
                        if (idx[k] < idx[k2]) {
                            if (pair_list_push(pl, idx[k], idx[k2]) != 0) {
                                rc = -1;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    free(cx);
    free(cy);
    free(cz);
    free(head);
    free(next);
    return rc;
}

/* Merges body j into body i's slot or vice versa (whichever is heavier
   survives). Conserves mass and momentum exactly; radius via sum of cubes. */
static int merge_pair(world *w, int i, int j) {
    body *bi = &w->bodies[i];
    body *bj = &w->bodies[j];
    double mi = bi->mass, mj = bj->mass;
    double total = mi + mj;
    int survivor = (mj > mi) ? j : i;
    int loser = (survivor == i) ? j : i;

    vec3 newpos = vec3_scale(vec3_add(vec3_scale(bi->pos, mi), vec3_scale(bj->pos, mj)), 1.0 / total);
    vec3 newvel = vec3_scale(vec3_add(vec3_scale(bi->vel, mi), vec3_scale(bj->vel, mj)), 1.0 / total);
    double newradius = cbrt(bi->radius * bi->radius * bi->radius + bj->radius * bj->radius * bj->radius);

    body *s = &w->bodies[survivor];
    s->mass = total;
    s->pos = newpos;
    s->vel = newvel;
    s->radius = newradius;
    w->bodies[loser].alive = 0;
    return survivor;
}

static int overlaps(const world *w, int i, int j) {
    const body *bi = &w->bodies[i];
    const body *bj = &w->bodies[j];
    if (!bi->alive || !bj->alive) return 0;
    double rsum = bi->radius + bj->radius;
    double d2 = vec3_len2(vec3_sub(bi->pos, bj->pos));
    return d2 < rsum * rsum;
}

int collision_merge(world *w) {
    if (!w) return -1;
    int total_merges = 0;
    int changed = 1;

    while (changed) {
        changed = 0;

        int *idx = malloc((size_t)(w->count > 0 ? w->count : 1) * sizeof(int));
        if (!idx) return -1;
        int m = 0;
        for (int i = 0; i < w->count; i++) {
            if (w->bodies[i].alive) idx[m++] = i;
        }

        if (m < 2) {
            free(idx);
            break;
        }

        pair_list pl = {0};
        int rc;
        if (m > COLLISION_GRID_THRESHOLD) {
            rc = collect_pairs_grid(w, idx, m, &pl);
        } else {
            rc = collect_pairs_brute(w, idx, m, &pl);
        }
        free(idx);
        if (rc != 0) {
            free(pl.items);
            return -1;
        }

        char *merged_this_pass = calloc((size_t)w->count, sizeof(char));
        if (!merged_this_pass) {
            free(pl.items);
            return -1;
        }

        for (int p = 0; p < pl.count; p++) {
            int a = pl.items[p].a, b = pl.items[p].b;
            if (merged_this_pass[a] || merged_this_pass[b]) continue;
            if (!overlaps(w, a, b)) continue;
            int survivor = merge_pair(w, a, b);
            int loser = (survivor == a) ? b : a;
            merged_this_pass[a] = 1;
            merged_this_pass[b] = 1;
            (void)survivor;
            (void)loser;
            total_merges++;
            changed = 1;
        }

        free(pl.items);
        free(merged_this_pass);
    }

    return total_merges;
}
