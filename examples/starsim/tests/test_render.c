#include "render.h"
#include "test.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_init_resize_free(void) {
    canvas c;
    CHECK(canvas_init(&c, 40, 20) == 0);
    CHECK(c.w == 40 && c.h == 20);
    CHECK(c.px[0] == 0);
    canvas_clear(&c, rgb(10, 20, 30));
    CHECK(c.px[0] == rgb(10, 20, 30));
    CHECK(c.px[(size_t)19 * 40 + 39] == rgb(10, 20, 30));

    CHECK(canvas_resize(&c, 80, 60) == 0);
    CHECK(c.w == 80 && c.h == 60);
    CHECK(c.px[0] == 0); /* cleared on resize */

    canvas_free(&c);
    CHECK(c.px == NULL && c.w == 0 && c.h == 0);
}

static void test_set_and_blend_math(void) {
    canvas c;
    canvas_init(&c, 10, 10);

    canvas_set(&c, 5, 5, rgb(1, 2, 3));
    CHECK(c.px[5 * 10 + 5] == rgb(1, 2, 3));

    /* alpha 128 of white over black is about 128 */
    canvas_blend(&c, 1, 1, rgb(255, 255, 255), 128);
    uint32_t p = c.px[1 * 10 + 1];
    CHECK(abs(rgb_r(p) - 128) <= 2);
    CHECK(abs(rgb_g(p) - 128) <= 2);
    CHECK(abs(rgb_b(p) - 128) <= 2);

    /* full alpha replaces */
    canvas_blend(&c, 2, 2, rgb(10, 20, 30), 255);
    CHECK(c.px[2 * 10 + 2] == rgb(10, 20, 30));

    /* zero alpha leaves untouched */
    canvas_set(&c, 3, 3, rgb(9, 9, 9));
    canvas_blend(&c, 3, 3, rgb(200, 200, 200), 0);
    CHECK(c.px[3 * 10 + 3] == rgb(9, 9, 9));

    canvas_free(&c);
}

static void test_add_saturates(void) {
    canvas c;
    canvas_init(&c, 4, 4);
    canvas_set(&c, 1, 1, rgb(200, 200, 200));
    canvas_add(&c, 1, 1, rgb(255, 255, 255), 1.0);
    uint32_t p = c.px[1 * 4 + 1];
    CHECK(rgb_r(p) == 255 && rgb_g(p) == 255 && rgb_b(p) == 255);

    /* moderate add is additive, not saturating early */
    canvas_clear(&c, 0);
    canvas_add(&c, 0, 0, rgb(100, 50, 10), 0.5);
    p = c.px[0];
    CHECK(rgb_r(p) == 50 && rgb_g(p) == 25 && rgb_b(p) == 5);

    canvas_free(&c);
}

static void test_disc_coverage(void) {
    canvas c;
    int w = 100, h = 100;
    canvas_init(&c, w, h);
    double r = 20.0;
    canvas_disc(&c, 50.0, 50.5, r, rgb(255, 255, 255), 255);

    double coverage = 0.0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) coverage += rgb_r(c.px[(size_t)y * w + x]) / 255.0;

    double expected = 3.14159265358979323846 * r * r;
    CHECK(fabs(coverage - expected) <= 0.05 * expected);

    canvas_free(&c);
}

static void test_line_endpoints_lit(void) {
    canvas c;
    canvas_init(&c, 50, 50);
    /* pixel-center coordinates so the endpoint falls fully in one pixel */
    canvas_line(&c, 5.5, 5.5, 40.5, 5.5, rgb(255, 255, 255), 255);
    CHECK(rgb_r(c.px[5 * 50 + 5]) > 200);
    CHECK(rgb_r(c.px[5 * 50 + 40]) > 200);

    canvas_clear(&c, 0);
    canvas_line(&c, 10.5, 10.5, 10.5, 30.5, rgb(255, 255, 255), 255);
    CHECK(rgb_r(c.px[10 * 50 + 10]) > 200);
    CHECK(rgb_r(c.px[30 * 50 + 10]) > 200);

    canvas_clear(&c, 0);
    canvas_line(&c, 5.5, 5.5, 30.5, 30.5, rgb(255, 255, 255), 255);
    CHECK(rgb_r(c.px[5 * 50 + 5]) > 100);
    CHECK(rgb_r(c.px[30 * 50 + 30]) > 100);

    canvas_free(&c);
}

static void test_ring(void) {
    canvas c;
    canvas_init(&c, 60, 60);
    canvas_ring(&c, 30.5, 30.5, 15.0, rgb(255, 255, 255), 255);
    /* on the ring, at a pixel center exactly r from the center */
    CHECK(rgb_r(c.px[30 * 60 + 45]) > 200);
    /* well inside the ring (hollow) stays dark */
    CHECK(rgb_r(c.px[30 * 60 + 30]) == 0);
    canvas_free(&c);
}

static void test_glow(void) {
    canvas c;
    canvas_init(&c, 60, 60);
    canvas_glow(&c, 30.0, 30.0, 20.0, rgb(255, 255, 255), 0.9);
    /* brighter at center than near the edge of the glow radius */
    uint32_t center = c.px[30 * 60 + 30];
    uint32_t edge = c.px[30 * 60 + 48];
    CHECK(rgb_r(center) > rgb_r(edge));
    CHECK(rgb_r(center) > 0);
    canvas_free(&c);
}

static void test_rect(void) {
    canvas c;
    canvas_init(&c, 20, 20);
    canvas_rect(&c, 5, 5, 10, 4, rgb(100, 150, 200), 255);
    CHECK(c.px[5 * 20 + 5] == rgb(100, 150, 200));
    CHECK(c.px[8 * 20 + 14] == rgb(100, 150, 200));
    CHECK(c.px[9 * 20 + 5] == 0); /* just below the rect */
    CHECK(c.px[5 * 20 + 15] == 0); /* just right of the rect */
    canvas_free(&c);
}

/* Draw far off-canvas shapes and every drawing primitive with extreme
 * coordinates, then verify a canary buffer surrounding the canvas memory
 * is untouched: nothing ever writes out of bounds. */
static void test_clipping_never_overflows(void) {
    int w = 32, h = 32;
    size_t n = (size_t)w * h;
    size_t pad = 4096;
    uint32_t *buf = malloc((pad * 2 + n) * sizeof(uint32_t));
    CHECK(buf != NULL);
    if (!buf) return;
    for (size_t i = 0; i < pad * 2 + n; i++) buf[i] = 0xABABABABu;

    canvas c;
    c.w = w;
    c.h = h;
    c.px = buf + pad;
    memset(c.px, 0, n * sizeof(uint32_t));

    canvas_disc(&c, -1e6, -1e6, 20.0, rgb(255, 255, 255), 255);
    canvas_disc(&c, 1e7, 1e7, 20.0, rgb(255, 255, 255), 255);
    canvas_disc(&c, 16.0, 16.0, 1e6, rgb(255, 255, 255), 255);
    canvas_glow(&c, -1e6, 1e6, 100.0, rgb(255, 255, 0), 1.0);
    canvas_ring(&c, 1e6, -1e6, 50.0, rgb(0, 255, 0), 255);
    canvas_line(&c, 1e7, 1e7, 1e7 + 50, 1e7 + 50, rgb(255, 0, 0), 255);
    canvas_line(&c, -1e7, 5.0, 1e7, 5.0, rgb(255, 0, 0), 255);
    canvas_rect(&c, -1000, -1000, 5000, 5000, rgb(0, 0, 255), 255);
    canvas_set(&c, -5, -5, rgb(1, 1, 1));
    canvas_set(&c, 1000000, 1000000, rgb(1, 1, 1));
    canvas_blend(&c, 100000, 100000, rgb(1, 1, 1), 255);
    canvas_blend(&c, -100000, -100000, rgb(1, 1, 1), 255);
    canvas_add(&c, -100000, 100000, rgb(1, 1, 1), 1.0);
    canvas_add(&c, 100000, -100000, rgb(1, 1, 1), 1.0);
    canvas_starfield(&c, 42, 1e6, -1e6);

    int ok = 1;
    for (size_t i = 0; i < pad; i++)
        if (buf[i] != 0xABABABABu) ok = 0;
    for (size_t i = pad + n; i < pad * 2 + n; i++)
        if (buf[i] != 0xABABABABu) ok = 0;
    CHECK(ok);

    free(buf);
}

static void test_ppm(void) {
    canvas c;
    canvas_init(&c, 64, 32);
    canvas_clear(&c, rgb(1, 2, 3));
    CHECK(canvas_write_ppm(&c, "build/test_render.ppm") == 0);

    FILE *f = fopen("build/test_render.ppm", "rb");
    CHECK(f != NULL);
    if (f) {
        char magic[3] = {0};
        int rw = 0, rh = 0, maxv = 0;
        CHECK(fscanf(f, "%2s", magic) == 1);
        CHECK(strcmp(magic, "P6") == 0);
        CHECK(fscanf(f, "%d %d %d", &rw, &rh, &maxv) == 3);
        CHECK(rw == 64 && rh == 32 && maxv == 255);
        fgetc(f); /* single whitespace before binary data */
        unsigned char px[3];
        CHECK(fread(px, 1, 3, f) == 3);
        CHECK(px[0] == 1 && px[1] == 2 && px[2] == 3);
        fclose(f);
    }

    canvas_free(&c);
}

static void test_starfield_deterministic_and_shifts(void) {
    canvas a, b;
    canvas_init(&a, 200, 150);
    canvas_init(&b, 200, 150);

    canvas_starfield(&a, 7, 0.0, 0.0);
    canvas_starfield(&b, 7, 0.0, 0.0);
    CHECK(memcmp(a.px, b.px, (size_t)200 * 150 * sizeof(uint32_t)) == 0);

    canvas_clear(&b, 0);
    canvas_starfield(&b, 7, 15.0, -8.0);
    CHECK(memcmp(a.px, b.px, (size_t)200 * 150 * sizeof(uint32_t)) != 0);

    canvas_clear(&b, 0);
    canvas_starfield(&b, 99, 0.0, 0.0);
    CHECK(memcmp(a.px, b.px, (size_t)200 * 150 * sizeof(uint32_t)) != 0);

    /* density roughly 1 star per 900 px^2 */
    int lit = 0;
    for (int i = 0; i < 200 * 150; i++)
        if (a.px[i] != 0) lit++;
    double expected = (200.0 * 150.0) / 900.0;
    CHECK(lit > expected * 0.3 && lit < expected * 3.0);

    canvas_free(&a);
    canvas_free(&b);
}

int main(void) {
    RUN(test_init_resize_free);
    RUN(test_set_and_blend_math);
    RUN(test_add_saturates);
    RUN(test_disc_coverage);
    RUN(test_line_endpoints_lit);
    RUN(test_ring);
    RUN(test_glow);
    RUN(test_rect);
    RUN(test_clipping_never_overflows);
    RUN(test_ppm);
    RUN(test_starfield_deterministic_and_shifts);
    return TEST_SUMMARY();
}
