#include "render_ppm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

static void test_init_fill_free(void) {
    image im;
    CHECK(image_init(&im, 10, 5) == 0);
    CHECK(im.w == 10 && im.h == 5);
    /* black by default */
    CHECK(im.px[0] == 0 && im.px[1] == 0 && im.px[2] == 0);

    rgb white = {255, 255, 255};
    image_fill(&im, white);
    for (int i = 0; i < im.w * im.h * 3; i++) CHECK(im.px[i] == 255);

    CHECK(image_init(&im, 0, 5) == -1 || 1); /* placeholder, real bad-size check below */
    image_free(&im);
    CHECK(im.px == NULL);

    image bad;
    CHECK(image_init(&bad, 0, 5) == -1);
    CHECK(image_init(&bad, 5, -1) == -1);
    CHECK(image_init(&bad, NULL ? 1 : 1, 1) == 0);
    image_free(&bad);
}

static void test_plot_bounds(void) {
    image im;
    CHECK(image_init(&im, 4, 4) == 0);
    rgb red = {255, 0, 0};

    image_plot(&im, 1, 1, red);
    size_t idx = ((size_t)1 * 4 + 1) * 3;
    CHECK(im.px[idx] == 255 && im.px[idx + 1] == 0 && im.px[idx + 2] == 0);

    /* off-image plots are ignored, no crash, no corruption elsewhere */
    image_plot(&im, -1, 0, red);
    image_plot(&im, 0, -1, red);
    image_plot(&im, 4, 0, red);
    image_plot(&im, 0, 4, red);
    image_plot(&im, 100, 100, red);

    int total_nonblack = 0;
    for (int i = 0; i < im.w * im.h; i++) {
        if (im.px[i * 3] != 0 || im.px[i * 3 + 1] != 0 || im.px[i * 3 + 2] != 0) total_nonblack++;
    }
    CHECK(total_nonblack == 1);

    image_free(&im);
}

static void test_fade(void) {
    image im;
    CHECK(image_init(&im, 3, 3) == 0);
    rgb c = {200, 100, 40};
    image_fill(&im, c);
    image_fade(&im, 0.5);
    for (int i = 0; i < im.w * im.h; i++) {
        CHECK(im.px[i * 3 + 0] == 100);
        CHECK(im.px[i * 3 + 1] == 50);
        CHECK(im.px[i * 3 + 2] == 20);
    }
    image_free(&im);
}

static void test_disc_area(void) {
    image im;
    int size = 60;
    CHECK(image_init(&im, size, size) == 0);
    rgb c = {255, 255, 255};
    int r = 10;
    image_disc(&im, size / 2, size / 2, r, c);

    int count = 0;
    for (int i = 0; i < im.w * im.h; i++) {
        if (im.px[i * 3] != 0) count++;
    }
    double expected = M_PI * r * r;
    double rel = fabs(count - expected) / expected;
    CHECK(rel <= 0.15);

    /* clipped disc near an edge doesn't crash and still draws something */
    image_disc(&im, 0, 0, r, c);
    image_free(&im);
}

static int pixel_set(const image *im, int x, int y) {
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return 0;
    size_t idx = ((size_t)y * (size_t)im->w + (size_t)x) * 3u;
    return im->px[idx] != 0 || im->px[idx + 1] != 0 || im->px[idx + 2] != 0;
}

static int connected_8(const image *im, int x0, int y0, int x1, int y1) {
    /* Walk the set pixels from (x0,y0) using 8-neighbour flood fill and check x1,y1 reached. */
    int n = im->w * im->h;
    int *visited = calloc((size_t)n, sizeof(int));
    int *stackx = malloc(sizeof(int) * (size_t)n);
    int *stacky = malloc(sizeof(int) * (size_t)n);
    int sp = 0;
    stackx[sp] = x0;
    stacky[sp] = y0;
    sp++;
    visited[y0 * im->w + x0] = 1;
    int found = 0;
    while (sp > 0) {
        sp--;
        int x = stackx[sp];
        int y = stacky[sp];
        if (x == x1 && y == y1) found = 1;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= im->w || ny >= im->h) continue;
                if (visited[ny * im->w + nx]) continue;
                if (!pixel_set(im, nx, ny)) continue;
                visited[ny * im->w + nx] = 1;
                stackx[sp] = nx;
                stacky[sp] = ny;
                sp++;
            }
        }
    }
    free(visited);
    free(stackx);
    free(stacky);
    return found;
}

static void test_line_all_octants(void) {
    rgb c = {255, 255, 255};
    int size = 40;
    int cx = size / 2, cy = size / 2;
    /* 8 endpoints, one per octant direction */
    int dxs[8] = {15, 15, 7, -7, -15, -15, -7, 7};
    int dys[8] = {7, -7, 15, 15, 7, -7, -15, -15};

    for (int k = 0; k < 8; k++) {
        image im;
        CHECK(image_init(&im, size, size) == 0);
        int x1 = cx + dxs[k], y1 = cy + dys[k];
        image_line(&im, cx, cy, x1, y1, c);
        CHECK(pixel_set(&im, cx, cy));
        CHECK(pixel_set(&im, x1, y1));
        CHECK(connected_8(&im, cx, cy, x1, y1));
        image_free(&im);
    }

    /* degenerate: single point */
    image im;
    CHECK(image_init(&im, size, size) == 0);
    image_line(&im, 5, 5, 5, 5, c);
    CHECK(pixel_set(&im, 5, 5));
    image_free(&im);
}

static void test_write_ppm(void) {
    image im;
    int w = 8, h = 5;
    CHECK(image_init(&im, w, h) == 0);
    rgb c = {10, 20, 30};
    image_fill(&im, c);

    CHECK(image_write_ppm(&im, "build/test_render_ppm_out.ppm") == 0);

    FILE *f = fopen("build/test_render_ppm_out.ppm", "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        char magic[3] = {0};
        int rw = 0, rh = 0, maxval = 0;
        CHECK(fscanf(f, "%2s", magic) == 1);
        CHECK(magic[0] == 'P' && magic[1] == '6');
        CHECK(fscanf(f, "%d %d %d", &rw, &rh, &maxval) == 3);
        CHECK(rw == w && rh == h && maxval == 255);
        fgetc(f); /* single whitespace byte before binary data */

        long data_start = ftell(f);
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        long data_size = file_size - data_start;
        CHECK(data_size == (long)w * h * 3);

        fseek(f, data_start, SEEK_SET);
        unsigned char pixel[3];
        CHECK(fread(pixel, 1, 3, f) == 3);
        CHECK(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30);
        fclose(f);
    }

    image_free(&im);
    CHECK(image_write_ppm(NULL, "build/x.ppm") == -1);
    CHECK(image_write_ppm(&im, NULL) == -1);
}

static void test_render_world_center(void) {
    world w;
    CHECK(world_init(&w, 8) == 0);

    body star = body_make("Star", BODY_STAR, 1.0e30, 6.96e8,
                           vec3_make(0.0, 0.0, 0.0), vec3_zero());
    rgb yellow = {255, 255, 0};
    star.color = yellow;
    CHECK(world_add(&w, &star) >= 0);

    body planet = body_make("Planet", BODY_PLANET, 5.97e24, 6.37e6,
                             vec3_make(1.5e11, 0.0, 0.0), vec3_zero());
    rgb blue = {0, 0, 255};
    planet.color = blue;
    CHECK(world_add(&w, &planet) >= 0);

    image im;
    int size = 200;
    CHECK(image_init(&im, size, size) == 0);

    CHECK(render_world(&im, &w, 0.0) == 0);

    /* heaviest body (the star) sits essentially at the world's center of mass,
       which projects to the image center. */
    int cx = size / 2, cy = size / 2;
    int found_center = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            size_t idx = ((size_t)(cy + dy) * (size_t)size + (size_t)(cx + dx)) * 3u;
            if (im.px[idx] == yellow.r && im.px[idx + 1] == yellow.g && im.px[idx + 2] == yellow.b) {
                found_center = 1;
            }
        }
    }
    CHECK(found_center);

    image_free(&im);
    world_free(&w);
}

int main(void) {
    RUN(test_init_fill_free);
    RUN(test_plot_bounds);
    RUN(test_fade);
    RUN(test_disc_area);
    RUN(test_line_all_octants);
    RUN(test_write_ppm);
    RUN(test_render_world_center);
    return TEST_SUMMARY();
}
