/* octree.h - Barnes-Hut octree gravity: O(n log n) approximate accelerations. */
#ifndef ORBIT_OCTREE_H
#define ORBIT_OCTREE_H

#include "gravity.h"

typedef struct octree octree; /* opaque */

typedef struct {
    double theta; /* opening angle, typical 0.3-0.8; 0 = exact direct summation */
} bh_params;

/* Builds a tree over the alive bodies of w. NULL on allocation failure or no alive bodies. */
octree *octree_build(const world *w);
void octree_free(octree *t);
int octree_node_count(const octree *t);
/* Acceleration at pos from the bodies in t (built from w); skip_index = body to exclude, or -1. */
vec3 octree_accel_at(const octree *t, const world *w, vec3 pos, int skip_index, double theta);
/* accel_fn: ctx is bh_params* (NULL = theta 0.5). Zeroes acc of dead bodies. */
void gravity_barnes_hut(world *w, void *ctx);

#endif
