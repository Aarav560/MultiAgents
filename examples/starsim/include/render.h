/* render.h - software rasterizer into a 32-bit framebuffer (0x00RRGGBB). All drawing is clipped. */
#ifndef STARSIM_RENDER_H
#define STARSIM_RENDER_H

#include <stdint.h>

typedef struct {
    int w, h;
    uint32_t *px;           /* w * h pixels, row-major, top row first */
} canvas;

int canvas_init(canvas *c, int w, int h);      /* allocates, clears to black; 0 or -1 */
int canvas_resize(canvas *c, int w, int h);    /* reallocates and clears; 0 or -1 */
void canvas_free(canvas *c);
void canvas_clear(canvas *c, uint32_t color);

/* Opaque pixel, and alpha blend (alpha 0..255) of color over the pixel. */
void canvas_set(canvas *c, int x, int y, uint32_t color);
void canvas_blend(canvas *c, int x, int y, uint32_t color, int alpha);
/* Additive, per-channel saturating: pixel += color * intensity (intensity 0..1+). */
void canvas_add(canvas *c, int x, int y, uint32_t color, double intensity);

/* Filled disc at sub-pixel centre with a 1-pixel anti-aliased edge; alpha 0..255. */
void canvas_disc(canvas *c, double cx, double cy, double r, uint32_t color, int alpha);
/* Additive radial glow: intensity * (1 - d/r)^2 for d < r. Used around stars. */
void canvas_glow(canvas *c, double cx, double cy, double r, uint32_t color, double intensity);
/* Circle outline (1 px, anti-aliased). */
void canvas_ring(canvas *c, double cx, double cy, double r, uint32_t color, int alpha);
/* Anti-aliased line (Xiaolin Wu), alpha 0..255. Lines far outside the canvas are culled cheaply. */
void canvas_line(canvas *c, double x0, double y0, double x1, double y1, uint32_t color, int alpha);
/* Filled rectangle, alpha blended (for HUD panels). */
void canvas_rect(canvas *c, int x, int y, int w, int h, uint32_t color, int alpha);
/* Deterministic background star field (seeded). ox/oy shift it (parallax); stars wrap around. */
void canvas_starfield(canvas *c, unsigned seed, double ox, double oy);

/* Writes binary P6. 0 or -1. */
int canvas_write_ppm(const canvas *c, const char *path);

/* 0xRRGGBB helpers */
static inline uint32_t rgb(int r, int g, int b) {
    return ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}
static inline int rgb_r(uint32_t c) { return (int)((c >> 16) & 255); }
static inline int rgb_g(uint32_t c) { return (int)((c >> 8) & 255); }
static inline int rgb_b(uint32_t c) { return (int)(c & 255); }

#endif
