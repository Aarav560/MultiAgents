/* collision.h - perfectly inelastic merging of overlapping bodies. */
#ifndef ORBIT_COLLISION_H
#define ORBIT_COLLISION_H

#include "world.h"

/* Merges every pair of alive bodies whose distance is strictly less than the
   sum of their radii, repeating until no overlaps remain (handles chains).
   Mass and momentum are conserved exactly; position is the mass-weighted mean;
   radius = cbrt(r1^3 + r2^3). The heavier body survives (keeps its name, kind,
   color and id); the lighter one gets alive = 0. Does not compact the world.
   Returns the number of merges performed (>= 0), or -1 on allocation failure. */
int collision_merge(world *w);

#endif
