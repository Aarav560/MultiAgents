/* render_ascii.h - terminal renderer for the world state. */
#ifndef ORBIT_RENDER_ASCII_H
#define ORBIT_RENDER_ASCII_H

#include "world.h"
#include <stddef.h>

/* Renders the world into an ASCII grid.
   Projects x-y centered on the center of mass.
   Writes rows lines of cols chars, each ending in '\n', NUL-terminated.
   Needs rows*(cols+1)+1 bytes (or returns -1).
   Scale: m per cell. If scale == 0, auto-fit using world_extent * 1.1.
   Glyph: star '*', planet 'O', moon 'o', asteroid ':', spacecraft 'A', particle '.'.
   Heavier bodies win a shared cell.
   Returns 0 on success, -1 if buffer is too small. */
int render_ascii(const world *w, char *buf, size_t buflen, int cols, int rows, double scale);

#endif
