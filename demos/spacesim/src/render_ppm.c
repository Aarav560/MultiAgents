/* render_ppm.c - RGB raster image buffer and PPM (P6) output. */
#include "render_ppm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int image_init(image *im, int w, int h) {
    if (im == NULL || w <= 0 || h <= 0) return -1;
    size_t n = (size_t)w * (size_t)h * 3u;
    unsigned char *px = calloc(n, 1);
    if (px == NULL) return -1;
    im->w = w;
    im->h = h;
    im->px = px;
    return 0;
}

void image_free(image *im) {
    if (im == NULL) return;
    free(im->px);
    im->px = NULL;
    im->w = 0;
    im->h = 0;
}

void image_fill(image *im, rgb c) {
    if (im == NULL || im->px == NULL) return;
    size_t count = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < count; i++) {
        im->px[i * 3 + 0] = c.r;
        im->px[i * 3 + 1] = c.g;
        im->px[i * 3 + 2] = c.b;
    }
}

void image_fade(image *im, double keep) {
    if (im == NULL || im->px == NULL) return;
    if (keep < 0.0) keep = 0.0;
    if (keep > 1.0) keep = 1.0;
    size_t n = (size_t)im->w * (size_t)im->h * 3u;
    for (size_t i = 0; i < n; i++) {
        im->px[i] = (unsigned char)(im->px[i] * keep + 0.5);
    }
}

void image_plot(image *im, int x, int y, rgb c) {
    if (im == NULL || im->px == NULL) return;
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    size_t idx = ((size_t)y * (size_t)im->w + (size_t)x) * 3u;
    im->px[idx + 0] = c.r;
    im->px[idx + 1] = c.g;
    im->px[idx + 2] = c.b;
}

void image_disc(image *im, int cx, int cy, int r, rgb c) {
    if (im == NULL || im->px == NULL || r < 0) return;
    int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= im->w) x1 = im->w - 1;
    if (y1 >= im->h) y1 = im->h - 1;
    long r2 = (long)r * (long)r;
    for (int y = y0; y <= y1; y++) {
        long dy = y - cy;
        for (int x = x0; x <= x1; x++) {
            long dx = x - cx;
            if (dx * dx + dy * dy <= r2) image_plot(im, x, y, c);
        }
    }
}

void image_line(image *im, int x0, int y0, int x1, int y1, rgb c) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int x = x0, y = y0;
    for (;;) {
        image_plot(im, x, y, c);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

int image_write_ppm(const image *im, const char *path) {
    if (im == NULL || im->px == NULL || path == NULL) return -1;
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    if (fprintf(f, "P6\n%d %d\n255\n", im->w, im->h) < 0) {
        fclose(f);
        return -1;
    }
    size_t n = (size_t)im->w * (size_t)im->h * 3u;
    size_t written = fwrite(im->px, 1, n, f);
    if (fclose(f) != 0 || written != n) return -1;
    return 0;
}

int render_world(image *im, const world *w, double scale) {
    if (im == NULL || im->px == NULL || w == NULL) return -1;

    vec3 com = world_com(w);

    if (scale <= 0.0) {
        double extent = world_extent(w) * 1.1;
        if (extent <= 0.0) extent = 1.0;
        double half = (im->w < im->h ? im->w : im->h) / 2.0;
        if (half < 1.0) half = 1.0;
        scale = extent / half;
    }

    double cx = im->w / 2.0;
    double cy = im->h / 2.0;

    for (int i = 0; i < w->count; i++) {
        const body *b = &w->bodies[i];
        if (!b->alive) continue;
        double px = cx + (b->pos.x - com.x) / scale;
        double py = cy - (b->pos.y - com.y) / scale; /* screen y grows downward */
        int ix = (int)floor(px + 0.5);
        int iy = (int)floor(py + 0.5);
        double r_px = b->radius / scale;
        int ir = (int)(r_px + 0.5);
        if (ir < 1) ir = 1;
        if (ir > 12) ir = 12;
        image_disc(im, ix, iy, ir, b->color);
    }

    return 0;
}
