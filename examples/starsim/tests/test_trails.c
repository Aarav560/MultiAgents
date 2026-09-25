#include "../tests/test.h"

#include "trails.h"

#include <stdlib.h>

/* Helper to create a test world */
static void setup_world(world *w) {
    CHECK(world_init(w, 10) == 0);
    w->G = 1.0;
}

static void test_trails_create_and_free(void) {
    trails *t = trails_create(5, 10);
    CHECK(t != NULL);
    trails_free(t);
}

static void test_trails_invalid_args(void) {
    trails *t = trails_create(0, 10);
    CHECK(t == NULL);

    t = trails_create(5, 0);
    CHECK(t == NULL);
}

TEST(trails_get_unknown_id) {
    trails *t = trails_create(5, 10);
    vec2 out[10];
    CHECK(trails_get(t, 99, out, 10) == 0);
    trails_free(t);
}

TEST(trails_clear) {
    trails *t = trails_create(5, 10);
    world w;
    setup_world(&w);

    /* Add a body and record */
    body b = body_make("Star", KIND_STAR, 1.0, 0.1, v2(0.0, 0.0), v2(0.0, 0.0),
                       0xFFFFFF);
    world_add(&w, &b);
    trails_record(t, &w);

    /* Verify trail exists */
    vec2 out[10];
    int count = trails_get(t, 0, out, 10);
    CHECK(count == 1);

    /* Clear and verify trail is gone */
    trails_clear(t);
    count = trails_get(t, 0, out, 10);
    CHECK(count == 0);

    world_free(&w);
    trails_free(t);
}

TEST(trails_skip_dust) {
    trails *t = trails_create(5, 10);
    world w;
    setup_world(&w);

    /* Add a star and dust */
    body b1 = body_make("Star", KIND_STAR, 1.0, 0.1, v2(1.0, 2.0), v2(0.0, 0.0),
                        0xFFFFFF);
    world_add(&w, &b1);

    body b2 = body_make("Dust", KIND_DUST, 1e-9, 0.001, v2(3.0, 4.0), v2(0.0, 0.0),
                        0x888888);
    world_add(&w, &b2);

    trails_record(t, &w);

    /* Star should have a trail */
    vec2 out[10];
    int count = trails_get(t, 0, out, 10);
    CHECK(count == 1);
    CHECK_NEAR(out[0].x, 1.0, 1e-9);
    CHECK_NEAR(out[0].y, 2.0, 1e-9);

    /* Dust should not have a trail */
    count = trails_get(t, 1, out, 10);
    CHECK(count == 0);

    world_free(&w);
    trails_free(t);
}

TEST(trails_ring_buffer) {
    trails *t = trails_create(5, 3);
    world w;
    setup_world(&w);

    body b = body_make("Star", KIND_STAR, 1.0, 0.1, v2(0.0, 0.0), v2(0.0, 0.0),
                       0xFFFFFF);
    world_add(&w, &b);

    /* Record 5 positions with max_points=3 (should wrap) */
    for (int step = 0; step < 5; step++) {
        w.b[0].pos = v2((double)step, (double)(step * 10));
        trails_record(t, &w);
    }

    /* Get trail - should be oldest-first */
    vec2 out[5];
    int count = trails_get(t, 0, out, 5);
    CHECK(count == 3); /* max_points */

    /* Positions should be steps 2, 3, 4 (oldest-first) */
    CHECK_NEAR(out[0].x, 2.0, 1e-9);
    CHECK_NEAR(out[0].y, 20.0, 1e-9);
    CHECK_NEAR(out[1].x, 3.0, 1e-9);
    CHECK_NEAR(out[1].y, 30.0, 1e-9);
    CHECK_NEAR(out[2].x, 4.0, 1e-9);
    CHECK_NEAR(out[2].y, 40.0, 1e-9);

    world_free(&w);
    trails_free(t);
}

TEST(trails_dead_body_releases_slot) {
    trails *t = trails_create(5, 10);
    world w;
    setup_world(&w);

    /* Add two bodies */
    body b1 = body_make("Star1", KIND_STAR, 1.0, 0.1, v2(1.0, 1.0), v2(0.0, 0.0),
                        0xFF0000);
    world_add(&w, &b1);

    body b2 = body_make("Star2", KIND_STAR, 1.0, 0.1, v2(2.0, 2.0), v2(0.0, 0.0),
                        0x00FF00);
    world_add(&w, &b2);

    trails_record(t, &w);

    /* Both should have trails */
    vec2 out[10];
    CHECK(trails_get(t, 0, out, 10) == 1);
    CHECK(trails_get(t, 1, out, 10) == 1);

    /* Kill the first body */
    w.b[0].alive = 0;
    world_compact(&w);
    trails_record(t, &w);

    /* First should be gone, second should still be there */
    CHECK(trails_get(t, 0, out, 10) == 0);
    CHECK(trails_get(t, 1, out, 10) == 1);

    world_free(&w);
    trails_free(t);
}

TEST(trails_slot_reuse) {
    trails *t = trails_create(2, 10);
    world w;
    setup_world(&w);

    /* Add first body and record */
    body b1 = body_make("Star1", KIND_STAR, 1.0, 0.1, v2(1.0, 1.0), v2(0.0, 0.0),
                        0xFF0000);
    int idx1 = world_add(&w, &b1);
    int id1 = w.b[idx1].id;
    trails_record(t, &w);

    /* Verify slot is used */
    vec2 out[10];
    CHECK(trails_get(t, id1, out, 10) == 1);

    /* Kill first body */
    w.b[idx1].alive = 0;
    world_compact(&w);
    trails_record(t, &w);

    /* Trail should be gone */
    CHECK(trails_get(t, id1, out, 10) == 0);

    /* Add a new body - should reuse the slot */
    body b2 = body_make("Star2", KIND_STAR, 1.0, 0.1, v2(2.0, 2.0), v2(0.0, 0.0),
                        0x00FF00);
    int idx2 = world_add(&w, &b2);
    int id2 = w.b[idx2].id;
    trails_record(t, &w);

    /* New body should have a trail */
    CHECK(trails_get(t, id2, out, 10) == 1);

    world_free(&w);
    trails_free(t);
}

TEST(trails_max_bodies_exceeded) {
    trails *t = trails_create(2, 10);
    world w;
    setup_world(&w);

    /* Add 3 bodies (exceeds max_bodies=2) */
    for (int i = 0; i < 3; i++) {
        body b = body_make("Star", KIND_STAR, 1.0, 0.1, v2((double)i, (double)i),
                           v2(0.0, 0.0), 0xFFFFFF);
        world_add(&w, &b);
    }

    trails_record(t, &w);

    /* At least the first two should have trails */
    vec2 out[10];
    int count1 = trails_get(t, 0, out, 10);
    int count2 = trails_get(t, 1, out, 10);
    int count3 = trails_get(t, 2, out, 10);

    /* Two should have trails, one should not (gracefully ignored) */
    int total = (count1 > 0 ? 1 : 0) + (count2 > 0 ? 1 : 0) + (count3 > 0 ? 1 : 0);
    CHECK(total == 2);

    world_free(&w);
    trails_free(t);
}

TEST(trails_multiple_records) {
    trails *t = trails_create(5, 5);
    world w;
    setup_world(&w);

    body b = body_make("Star", KIND_STAR, 1.0, 0.1, v2(0.0, 0.0), v2(0.0, 0.0),
                       0xFFFFFF);
    world_add(&w, &b);

    /* Record several positions */
    vec2 positions[] = {{1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}, {4.0, 4.0}};
    for (int i = 0; i < 4; i++) {
        w.b[0].pos = positions[i];
        trails_record(t, &w);
    }

    /* Get trail */
    vec2 out[10];
    int count = trails_get(t, 0, out, 10);
    CHECK(count == 4);

    /* Verify order (oldest-first) */
    for (int i = 0; i < 4; i++) {
        CHECK_NEAR(out[i].x, positions[i].x, 1e-9);
        CHECK_NEAR(out[i].y, positions[i].y, 1e-9);
    }

    world_free(&w);
    trails_free(t);
}

TEST(trails_partial_read) {
    trails *t = trails_create(5, 10);
    world w;
    setup_world(&w);

    body b = body_make("Star", KIND_STAR, 1.0, 0.1, v2(0.0, 0.0), v2(0.0, 0.0),
                       0xFFFFFF);
    world_add(&w, &b);

    /* Record 5 positions */
    for (int i = 0; i < 5; i++) {
        w.b[0].pos = v2((double)i, (double)i);
        trails_record(t, &w);
    }

    /* Request only 3 positions */
    vec2 out[10];
    int count = trails_get(t, 0, out, 3);
    CHECK(count == 3);

    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(out[i].x, (double)i, 1e-9);
        CHECK_NEAR(out[i].y, (double)i, 1e-9);
    }

    world_free(&w);
    trails_free(t);
}

TEST_SUMMARY();
