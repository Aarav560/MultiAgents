/* render_ppm.h - RGB raster image buffer and PPM (P6) output for the simulated world. */
#ifndef ORBIT_RENDER_PPM_H
#define ORBIT_RENDER_PPM_H

#include "body.h"
#include "world.h"

typedef struct {
    int w, h;
    unsigned char *px; /* RGB, 3 bytes per pixel, row-major, top-left origin */
} image;

/* Allocates w*h*3 bytes, all zero (black). Returns 0, or -1 on bad size or allocation failure. */
int image_init(image *im, int w, int h);
void image_free(image *im);

/* Fills every pixel with c. */
void image_fill(image *im, rgb c);

/* Multiplies every channel of every pixel by keep, for motion trails. keep is clamped to [0, 1]. */
void image_fade(image *im, double keep);

/* Sets one pixel. Out-of-bounds coordinates are ignored. */
void image_plot(image *im, int x, int y, rgb c);

/* Filled circle of radius r centered at (cx, cy), clipped to the image. */
void image_disc(image *im, int cx, int cy, int r, rgb c);

/* Bresenham line from (x0, y0) to (x1, y1), clipped per-pixel. */
void image_line(image *im, int x0, int y0, int x1, int y1, rgb c);

/* Writes a binary P6 PPM file. Returns 0, or -1 on error. */
int image_write_ppm(const image *im, const char *path);

/* Draws every alive body of w as a disc, projected on the x-y plane and centered on the
   center of mass. scale is meters per pixel; 0 means auto-fit from world_extent.
   Disc radius = max(1, body.radius / scale), capped at 12 px. Returns 0, or -1 on error. */
int render_world(image *im, const world *w, double scale);

#endif
