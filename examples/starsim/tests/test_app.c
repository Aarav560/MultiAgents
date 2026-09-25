/* test_app.c - headless end-to-end: every scenario renders a non-blank frame of the right size. */
#include "app.h"
#include "scenario.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>

#define W 640
#define H 400

/* Reads a binary P6 file; returns the number of lit pixels, or -1 if malformed. */
static long count_lit(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int maxv = 0;
    long lit = -1;
    if (fscanf(f, "P6 %d %d %d", w, h, &maxv) == 3 && maxv == 255 && fgetc(f) != EOF) {
        long n = (long)*w * *h, got = 0;
        lit = 0;
        unsigned char px[3];
        while (got < n && fread(px, 1, 3, f) == 3) {
            if (px[0] > 60 || px[1] > 60 || px[2] > 60) lit++;
            got++;
        }
        if (got != n || fgetc(f) != EOF) lit = -1;   /* exact size: no short or trailing data */
    }
    fclose(f);
    return lit;
}

static void test_all_scenarios(void) {
    for (int i = 0; i < scenario_count(); i++) {
        char path[64];
        snprintf(path, sizeof path, "build/test_app_%d.ppm", i + 1);
        CHECK(app_render_frames(i, W, H, 30, path) == 0);
        int w = 0, h = 0;
        long lit = count_lit(path, &w, &h);
        CHECK(w == W && h == H);
        CHECK(lit > 500);
        if (lit <= 500) fprintf(stderr, "  scenario %d: %ld lit pixels\n", i + 1, lit);
    }
}

static void test_bad_input(void) {
    CHECK(app_render_frames(99, W, H, 1, "build/test_app_bad.ppm") != 0);
    CHECK(app_render_frames(0, 0, H, 1, "build/test_app_bad.ppm") != 0);
}

int main(void) {
    CHECK(scenario_count() == 7);
    RUN(test_all_scenarios);
    RUN(test_bad_input);
    return TEST_SUMMARY();
}
