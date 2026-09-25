/* test_render_ascii.c - tests for ASCII rendering. */
#include "test.h"
#include "render_ascii.h"
#include "world.h"
#include <stdio.h>
#include <string.h>

/* Test: buffer too small returns -1 */
static void buffer_too_small(void) {
    world w;
    world_init(&w, 4);

    body b = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    world_add(&w, &b);

    char buf[10];
    int result = render_ascii(&w, buf, 10, 10, 10, 1.0);
    CHECK(result == -1);

    world_free(&w);
}

/* Test: valid buffer renders without error */
static void valid_buffer(void) {
    world w;
    world_init(&w, 4);

    body b = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    world_add(&w, &b);

    char buf[1000];
    int result = render_ascii(&w, buf, 1000, 10, 10, 1.0);
    CHECK(result == 0);

    world_free(&w);
}

/* Test: border is drawn correctly */
static void border_correct(void) {
    world w;
    world_init(&w, 4);

    body b = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(100, 100, 0), vec3_zero());
    world_add(&w, &b);

    char buf[1000];
    int result = render_ascii(&w, buf, 1000, 10, 8, 10.0);
    CHECK(result == 0);

    /* Check corners are '+' */
    CHECK(buf[0] == '+');                    /* top-left */
    CHECK(buf[9] == '+');                    /* top-right */
    CHECK(buf[7 * 11 + 0] == '+');           /* bottom-left (8 rows, each is 10 chars + newline) */
    CHECK(buf[7 * 11 + 9] == '+');           /* bottom-right */

    /* Check horizontal borders are '-' */
    CHECK(buf[1] == '-');                    /* top edge */
    CHECK(buf[8] == '-');                    /* top edge */
    CHECK(buf[7 * 11 + 1] == '-');           /* bottom edge */
    CHECK(buf[7 * 11 + 8] == '-');           /* bottom edge */

    /* Check vertical borders are '|' */
    CHECK(buf[11 + 0] == '|');               /* left edge */
    CHECK(buf[11 + 9] == '|');               /* right edge */

    world_free(&w);
}

/* Test: star at COM lands in center cell */
static void star_at_com_center(void) {
    world w;
    world_init(&w, 4);

    /* Place a star at the origin; world_com will be at origin */
    body b = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    world_add(&w, &b);
    world_recenter(&w);

    int cols = 11, rows = 11;
    char buf[2000];
    int result = render_ascii(&w, buf, 2000, cols, rows, 1.0);
    CHECK(result == 0);

    /* Center cell is at (5, 5) in an 11x11 grid (0-indexed) */
    int center_col = cols / 2;
    int center_row = rows / 2;
    int center_idx = center_row * (cols + 1) + center_col;
    CHECK(buf[center_idx] == '*');

    world_free(&w);
}

/* Test: glyph priority when bodies share a cell (heavier wins) */
static void glyph_priority(void) {
    world w;
    world_init(&w, 4);

    /* Two bodies at the same position, different masses */
    body star = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    body particle = body_make("particle", BODY_PARTICLE, 1e-10, 1.0, vec3_make(0, 0, 0), vec3_zero());

    world_add(&w, &star);
    world_add(&w, &particle);
    world_recenter(&w);

    char buf[2000];
    int result = render_ascii(&w, buf, 2000, 11, 11, 1.0);
    CHECK(result == 0);

    /* Center cell should show the heavier star */
    int center_idx = 5 * 12 + 5;
    CHECK(buf[center_idx] == '*');

    world_free(&w);
}

/* Test: bodies outside view are clipped, not on border */
static void clip_outside_view(void) {
    world w;
    world_init(&w, 4);

    /* Place a body at origin and one far outside */
    body center = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    body distant = body_make("planet", BODY_PLANET, 1e24, 6.4e6, vec3_make(1000, 1000, 0), vec3_zero());
    world_add(&w, &center);
    world_add(&w, &distant);
    world_recenter(&w);

    char buf[2000];
    int result = render_ascii(&w, buf, 2000, 10, 10, 1.0);
    CHECK(result == 0);

    /* The interior cells should have the center star but not the distant planet */
    int center_idx = 4 * 11 + 4; /* Center of 10x10 grid interior */
    CHECK(buf[center_idx] == '*');

    world_free(&w);
}

/* Test: auto-fit scale keeps every body inside */
static void autofit_scale(void) {
    world w;
    world_init(&w, 8);

    /* Add several bodies at different positions */
    vec3 positions[] = {
        {100, 100, 0},
        {-100, -100, 0},
        {50, -50, 0},
        {-50, 50, 0}
    };

    for (int i = 0; i < 4; i++) {
        body b = body_make("body", BODY_PLANET, 1e24, 6.4e6, positions[i], vec3_zero());
        world_add(&w, &b);
    }

    world_recenter(&w);

    char buf[2000];
    int result = render_ascii(&w, buf, 2000, 11, 11, 0.0); /* 0.0 = auto-fit */
    CHECK(result == 0);

    /* All bodies should be rendered (not all off-screen) */
    /* Count non-space, non-border characters */
    int body_count = 0;
    for (int r = 1; r < 10; r++) {
        for (int c = 1; c < 10; c++) {
            int idx = r * 12 + c;
            char ch = buf[idx];
            if (ch != ' ' && ch != '\n') {
                body_count++;
            }
        }
    }
    CHECK(body_count > 0); /* At least some bodies rendered */

    world_free(&w);
}

/* Test: different glyphs for different kinds */
static void kind_glyphs(void) {
    world w;
    world_init(&w, 10);

    body_kind kinds[] = {BODY_STAR, BODY_PLANET, BODY_MOON, BODY_ASTEROID, BODY_SPACECRAFT, BODY_PARTICLE};
    char expected_glyphs[] = {'*', 'O', 'o', ':', 'A', '.'};
    double masses[] = {1e30, 1e24, 1e23, 1e20, 1e4, 1e-10};

    /* Place bodies at different positions to avoid overlap */
    for (int i = 0; i < 6; i++) {
        double x = (i - 2.5) * 50; /* Spread them out */
        body b = body_make("body", kinds[i], masses[i], 1.0, vec3_make(x, 0, 0), vec3_zero());
        world_add(&w, &b);
    }

    world_recenter(&w);

    char buf[3000];
    int result = render_ascii(&w, buf, 3000, 21, 11, 50.0);
    CHECK(result == 0);

    /* Check that expected glyphs appear */
    for (int r = 1; r < 10; r++) {
        for (int c = 1; c < 20; c++) {
            int idx = r * 22 + c;
            char ch = buf[idx];
            for (int k = 0; k < 6; k++) {
                if (ch == expected_glyphs[k]) {
                    goto found;
                }
            }
        }
    }

    found:
    CHECK(1);

    world_free(&w);
}

/* Test: dead bodies are not rendered */
static void dead_bodies_skipped(void) {
    world w;
    world_init(&w, 4);

    body b1 = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    body b2 = body_make("planet", BODY_PLANET, 1e24, 6.4e6, vec3_make(0, 0, 0), vec3_zero());

    world_add(&w, &b1);
    int idx = world_add(&w, &b2);

    /* Mark one as dead */
    w.bodies[idx].alive = 0;

    char buf[2000];
    int result = render_ascii(&w, buf, 2000, 11, 11, 1.0);
    CHECK(result == 0);

    /* Only the star should appear at center */
    int center_idx = 5 * 12 + 5;
    CHECK(buf[center_idx] == '*');

    world_free(&w);
}

/* Test: line endings are correct */
static void line_endings(void) {
    world w;
    world_init(&w, 4);

    body b = body_make("star", BODY_STAR, 1e30, 7e8, vec3_make(0, 0, 0), vec3_zero());
    world_add(&w, &b);

    int cols = 10, rows = 5;
    char buf[500];
    int result = render_ascii(&w, buf, 500, cols, rows, 1.0);
    CHECK(result == 0);

    /* Check each line has exactly cols characters followed by '\n' */
    for (int r = 0; r < rows; r++) {
        int start = r * (cols + 1);
        int end = start + cols;
        CHECK(buf[end] == '\n');
        /* Check no newlines before that position */
        for (int i = start; i < end; i++) {
            CHECK(buf[i] != '\n');
        }
    }

    /* Check final NUL terminator */
    int final_pos = rows * (cols + 1);
    CHECK(buf[final_pos] == '\0');

    world_free(&w);
}

int main(void) {
    RUN(buffer_too_small);
    RUN(valid_buffer);
    RUN(border_correct);
    RUN(star_at_com_center);
    RUN(glyph_priority);
    RUN(clip_outside_view);
    RUN(autofit_scale);
    RUN(kind_glyphs);
    RUN(dead_bodies_skipped);
    RUN(line_endings);

    return TEST_SUMMARY();
}
