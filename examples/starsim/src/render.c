/* render.c - software rasterizer into a 32-bit framebuffer (0x00RRGGBB). */
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int clamp255i(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

int canvas_init(canvas *c, int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    c->px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!c->px) return -1;
    c->w = w;
    c->h = h;
    return 0;
}

int canvas_resize(canvas *c, int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!px) return -1;
    free(c->px);
    c->px = px;
    c->w = w;
    c->h = h;
    return 0;
}

void canvas_free(canvas *c) {
    free(c->px);
    c->px = NULL;
    c->w = c->h = 0;
}

void canvas_clear(canvas *c, uint32_t color) {
    size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++) c->px[i] = color;
}

void canvas_set(canvas *c, int x, int y, uint32_t color) {
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return;
    c->px[(size_t)y * c->w + x] = color;
}

void canvas_blend(canvas *c, int x, int y, uint32_t color, int alpha) {
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return;
    if (alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    uint32_t *p = &c->px[(size_t)y * c->w + x];
    uint32_t old = *p;
    int or_ = rgb_r(old), og = rgb_g(old), ob = rgb_b(old);
    int nr = rgb_r(color), ng = rgb_g(color), nb = rgb_b(color);
    int r = or_ + ((nr - or_) * alpha + 127) / 255;
    int g = og + ((ng - og) * alpha + 127) / 255;
    int b = ob + ((nb - ob) * alpha + 127) / 255;
    *p = rgb(clamp255i(r), clamp255i(g), clamp255i(b));
}

void canvas_add(canvas *c, int x, int y, uint32_t color, double intensity) {
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return;
    if (intensity <= 0.0) return;
    uint32_t *p = &c->px[(size_t)y * c->w + x];
    uint32_t old = *p;
    int r = rgb_r(old) + (int)lround(rgb_r(color) * intensity);
    int g = rgb_g(old) + (int)lround(rgb_g(color) * intensity);
    int b = rgb_b(old) + (int)lround(rgb_b(color) * intensity);
    *p = rgb(clamp255i(r), clamp255i(g), clamp255i(b));
}

void canvas_disc(canvas *c, double cx, double cy, double r, uint32_t color, int alpha) {
    if (r <= 0.0) return;
    if (cx + r < -1.0 || cx - r > c->w + 1.0 || cy + r < -1.0 || cy - r > c->h + 1.0) return;
    int ix0 = clampi((int)floor(cx - r - 1.0), 0, c->w - 1);
    int ix1 = clampi((int)ceil(cx + r + 1.0), 0, c->w - 1);
    int iy0 = clampi((int)floor(cy - r - 1.0), 0, c->h - 1);
    int iy1 = clampi((int)ceil(cy + r + 1.0), 0, c->h - 1);
    for (int y = iy0; y <= iy1; y++) {
        double dy = (y + 0.5) - cy;
        for (int x = ix0; x <= ix1; x++) {
            double dx = (x + 0.5) - cx;
            double d = sqrt(dx * dx + dy * dy);
            double cov = (r + 0.5) - d;
            if (cov <= 0.0) continue;
            if (cov > 1.0) cov = 1.0;
            canvas_blend(c, x, y, color, (int)lround(alpha * cov));
        }
    }
}

void canvas_glow(canvas *c, double cx, double cy, double r, uint32_t color, double intensity) {
    if (r <= 0.0 || intensity <= 0.0) return;
    if (cx + r < 0.0 || cx - r > c->w || cy + r < 0.0 || cy - r > c->h) return;
    int ix0 = clampi((int)floor(cx - r), 0, c->w - 1);
    int ix1 = clampi((int)ceil(cx + r), 0, c->w - 1);
    int iy0 = clampi((int)floor(cy - r), 0, c->h - 1);
    int iy1 = clampi((int)ceil(cy + r), 0, c->h - 1);
    for (int y = iy0; y <= iy1; y++) {
        double dy = (y + 0.5) - cy;
        for (int x = ix0; x <= ix1; x++) {
            double dx = (x + 0.5) - cx;
            double d = sqrt(dx * dx + dy * dy);
            if (d >= r) continue;
            double t = 1.0 - d / r;
            canvas_add(c, x, y, color, intensity * t * t);
        }
    }
}

void canvas_ring(canvas *c, double cx, double cy, double r, uint32_t color, int alpha) {
    if (r <= 0.0) return;
    double outer = r + 1.5;
    if (cx + outer < -1.0 || cx - outer > c->w + 1.0 || cy + outer < -1.0 || cy - outer > c->h + 1.0) return;
    int ix0 = clampi((int)floor(cx - outer), 0, c->w - 1);
    int ix1 = clampi((int)ceil(cx + outer), 0, c->w - 1);
    int iy0 = clampi((int)floor(cy - outer), 0, c->h - 1);
    int iy1 = clampi((int)ceil(cy + outer), 0, c->h - 1);
    for (int y = iy0; y <= iy1; y++) {
        double dy = (y + 0.5) - cy;
        for (int x = ix0; x <= ix1; x++) {
            double dx = (x + 0.5) - cx;
            double d = sqrt(dx * dx + dy * dy);
            double cov = 1.0 - fabs(d - r);
            if (cov <= 0.0) continue;
            if (cov > 1.0) cov = 1.0;
            canvas_blend(c, x, y, color, (int)lround(alpha * cov));
        }
    }
}

/* -- line: Liang-Barsky clip to a slightly padded canvas rect, then Xiaolin Wu AA raster -- */

static int clip_line(double *x0, double *y0, double *x1, double *y1, double xmin, double ymin,
                      double xmax, double ymax) {
    double dx = *x1 - *x0, dy = *y1 - *y0;
    double t0 = 0.0, t1 = 1.0;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {*x0 - xmin, xmax - *x0, *y0 - ymin, ymax - *y0};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return 0;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0.0) {
                if (t > t1) return 0;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return 0;
                if (t < t1) t1 = t;
            }
        }
    }
    double nx0 = *x0 + t0 * dx, ny0 = *y0 + t0 * dy;
    double nx1 = *x0 + t1 * dx, ny1 = *y0 + t1 * dy;
    *x0 = nx0;
    *y0 = ny0;
    *x1 = nx1;
    *y1 = ny1;
    return 1;
}

static inline double fpart_(double x) { return x - floor(x); }
static inline double rfpart_(double x) { return 1.0 - fpart_(x); }

void canvas_line(canvas *c, double x0, double y0, double x1, double y1, uint32_t color, int alpha) {
    double xmin = -2.0, ymin = -2.0, xmax = (double)c->w + 2.0, ymax = (double)c->h + 2.0;
    if (!clip_line(&x0, &y0, &x1, &y1, xmin, ymin, xmax, ymax)) return;

    int steep = fabs(y1 - y0) > fabs(x1 - x0);
    if (steep) {
        double t;
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        double t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    double dx = x1 - x0, dy = y1 - y0;
    double gradient = (dx == 0.0) ? 1.0 : dy / dx;

    double xend = round(x0);
    double yend = y0 + gradient * (xend - x0);
    double xgap = rfpart_(x0 + 0.5);
    int xpx1 = (int)xend;
    int ypx1 = (int)floor(yend);
    if (steep) {
        canvas_blend(c, ypx1, xpx1, color, (int)lround(alpha * rfpart_(yend) * xgap));
        canvas_blend(c, ypx1 + 1, xpx1, color, (int)lround(alpha * fpart_(yend) * xgap));
    } else {
        canvas_blend(c, xpx1, ypx1, color, (int)lround(alpha * rfpart_(yend) * xgap));
        canvas_blend(c, xpx1, ypx1 + 1, color, (int)lround(alpha * fpart_(yend) * xgap));
    }
    double intery = yend + gradient;

    xend = round(x1);
    yend = y1 + gradient * (xend - x1);
    xgap = fpart_(x1 + 0.5);
    int xpx2 = (int)xend;
    int ypx2 = (int)floor(yend);
    if (steep) {
        canvas_blend(c, ypx2, xpx2, color, (int)lround(alpha * rfpart_(yend) * xgap));
        canvas_blend(c, ypx2 + 1, xpx2, color, (int)lround(alpha * fpart_(yend) * xgap));
    } else {
        canvas_blend(c, xpx2, ypx2, color, (int)lround(alpha * rfpart_(yend) * xgap));
        canvas_blend(c, xpx2, ypx2 + 1, color, (int)lround(alpha * fpart_(yend) * xgap));
    }

    if (steep) {
        for (int x = xpx1 + 1; x < xpx2; x++) {
            int y = (int)floor(intery);
            canvas_blend(c, y, x, color, (int)lround(alpha * rfpart_(intery)));
            canvas_blend(c, y + 1, x, color, (int)lround(alpha * fpart_(intery)));
            intery += gradient;
        }
    } else {
        for (int x = xpx1 + 1; x < xpx2; x++) {
            int y = (int)floor(intery);
            canvas_blend(c, x, y, color, (int)lround(alpha * rfpart_(intery)));
            canvas_blend(c, x, y + 1, color, (int)lround(alpha * fpart_(intery)));
            intery += gradient;
        }
    }
}

void canvas_rect(canvas *c, int x, int y, int w, int h, uint32_t color, int alpha) {
    if (w <= 0 || h <= 0 || alpha <= 0) return;
    int x0 = clampi(x, 0, c->w);
    int x1 = clampi(x + w, 0, c->w);
    int y0 = clampi(y, 0, c->h);
    int y1 = clampi(y + h, 0, c->h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) canvas_blend(c, xx, yy, color, alpha);
}

/* -- deterministic starfield -- */

#define STAR_CELL 30 /* ~ sqrt(900) */

static uint32_t hash2(int x, int y, unsigned seed, unsigned salt) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2654435761u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

void canvas_starfield(canvas *c, unsigned seed, double ox, double oy) {
    int cx0 = (int)floor(ox / STAR_CELL) - 1;
    int cx1 = (int)floor((ox + c->w) / STAR_CELL) + 1;
    int cy0 = (int)floor(oy / STAR_CELL) - 1;
    int cy1 = (int)floor((oy + c->h) / STAR_CELL) + 1;
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            uint32_t h1 = hash2(cx, cy, seed, 1);
            uint32_t h2 = hash2(cx, cy, seed, 2);
            uint32_t h3 = hash2(cx, cy, seed, 3);
            uint32_t h4 = hash2(cx, cy, seed, 4);
            double jx = (double)(h1 % 1000u) / 1000.0 * STAR_CELL;
            double jy = (double)(h2 % 1000u) / 1000.0 * STAR_CELL;
            double wx = cx * (double)STAR_CELL + jx;
            double wy = cy * (double)STAR_CELL + jy;
            double sx = wx - ox;
            double sy = wy - oy;
            int px = (int)floor(sx);
            int py = (int)floor(sy);
            if (px < 0 || px >= c->w || py < 0 || py >= c->h) continue;

            int base = 130 + (int)(h3 % 126u); /* 130..255 */
            int tint = (int)(h4 % 30u) - 15;   /* slight color variation */
            int r = clamp255i(base + tint / 2);
            int g = clamp255i(base + tint / 3);
            int b = clamp255i(base + tint);
            uint32_t color = rgb(r, g, b);

            canvas_set(c, px, py, color);
            if (h3 % 20u == 0u) {
                /* occasional bright 2px star */
                canvas_set(c, clampi(px + 1, 0, c->w - 1), py, color);
                canvas_set(c, px, clampi(py + 1, 0, c->h - 1), color);
            }
        }
    }
}

int canvas_write_ppm(const canvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fprintf(f, "P6\n%d %d\n255\n", c->w, c->h) < 0) {
        fclose(f);
        return -1;
    }
    unsigned char *row = malloc((size_t)c->w * 3);
    if (!row) {
        fclose(f);
        return -1;
    }
    for (int y = 0; y < c->h; y++) {
        for (int x = 0; x < c->w; x++) {
            uint32_t p = c->px[(size_t)y * c->w + x];
            row[x * 3 + 0] = (unsigned char)rgb_r(p);
            row[x * 3 + 1] = (unsigned char)rgb_g(p);
            row[x * 3 + 2] = (unsigned char)rgb_b(p);
        }
        if (fwrite(row, 1, (size_t)c->w * 3, f) != (size_t)c->w * 3) {
            free(row);
            fclose(f);
            return -1;
        }
    }
    free(row);
    if (fclose(f) != 0) return -1;
    return 0;
}
