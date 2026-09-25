/* trails.h - recent positions of bodies, keyed by body id, for drawing orbit trails. */
#ifndef STARSIM_TRAILS_H
#define STARSIM_TRAILS_H

#include "sim.h"

typedef struct trails trails;

/* Keeps up to max_points positions for up to max_bodies bodies (the first bodies seen). */
trails *trails_create(int max_bodies, int max_points);
void trails_free(trails *t);
void trails_clear(trails *t);
/* Appends the current position of every alive body (ring buffer per body). Slots of bodies that
   are no longer alive are released and may be reused. Bodies of kind KIND_DUST are skipped. */
void trails_record(trails *t, const world *w);
/* Copies the trail of body id oldest-first into out (up to max_out points); returns the count
   (0 if the body has no trail). */
int trails_get(const trails *t, int id, vec2 *out, int max_out);

#endif
