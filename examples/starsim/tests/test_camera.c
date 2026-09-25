#include "camera.h"
#include "test.h"

static void test_to_screen_world_roundtrip(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};

    vec2 world_pts[] = {
        v2(0.0, 0.0),
        v2(100.0, 50.0),
        v2(-50.0, -100.0),
        v2(0.5, 0.5)
    };

    for (int i = 0; i < 4; i++) {
        vec2 screen = cam_to_screen(&c, world_pts[i]);
        vec2 world_back = cam_to_world(&c, screen);
        CHECK_NEAR(world_back.x, world_pts[i].x, 1e-9);
        CHECK_NEAR(world_back.y, world_pts[i].y, 1e-9);
    }
}

static void test_world_origin_to_screen_centre(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};

    vec2 screen = cam_to_screen(&c, v2(0.0, 0.0));
    CHECK_NEAR(screen.x, 640.0, 1e-9);
    CHECK_NEAR(screen.y, 400.0, 1e-9);
}

static void test_y_flip(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};

    vec2 above = cam_to_screen(&c, v2(0.0, 10.0));
    vec2 below = cam_to_screen(&c, v2(0.0, -10.0));

    CHECK(above.y < 400.0);
    CHECK(below.y > 400.0);
}

static void test_zoom_at_keeps_point_fixed(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};
    vec2 screen_pt = v2(500.0, 300.0);

    vec2 world_pt_before = cam_to_world(&c, screen_pt);

    cam_zoom_at(&c, screen_pt, 2.0);

    vec2 world_pt_after = cam_to_world(&c, screen_pt);

    CHECK_NEAR(world_pt_after.x, world_pt_before.x, 1e-9);
    CHECK_NEAR(world_pt_after.y, world_pt_before.y, 1e-9);
}

static void test_zoom_clamps(void) {
    camera c1 = {v2(0.0, 0.0), 1.0, 1280, 800};
    vec2 screen_pt = v2(640.0, 400.0);

    cam_zoom_at(&c1, screen_pt, 1e13);
    CHECK_NEAR(c1.scale, 1e12, 1e-9);

    camera c2 = {v2(0.0, 0.0), 1.0, 1280, 800};
    cam_zoom_at(&c2, screen_pt, 1e-13);
    CHECK_NEAR(c2.scale, 1e-12, 1e-9);
}

static void test_pan(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};

    cam_pan_pixels(&c, 100.0, 0.0);
    CHECK_NEAR(c.center.x, -100.0, 1e-9);
    CHECK_NEAR(c.center.y, 0.0, 1e-9);

    cam_pan_pixels(&c, 0.0, 50.0);
    CHECK_NEAR(c.center.x, -100.0, 1e-9);
    CHECK_NEAR(c.center.y, 50.0, 1e-9);
}

static void test_fit_uses_smaller_side(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};
    cam_fit(&c, v2(5.0, 3.0), 1.0);

    CHECK_NEAR(c.center.x, 5.0, 1e-9);
    CHECK_NEAR(c.center.y, 3.0, 1e-9);
    CHECK_NEAR(c.scale, 400.0, 1e-9);
}

static void test_fit_wider_screen(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1000, 400};
    cam_fit(&c, v2(0.0, 0.0), 1.0);

    CHECK_NEAR(c.scale, 200.0, 1e-9);
}

static void test_follow_t_1_reaches_target(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};
    vec2 target = v2(100.0, 50.0);

    cam_follow(&c, target, 1.0);

    CHECK_NEAR(c.center.x, 100.0, 1e-9);
    CHECK_NEAR(c.center.y, 50.0, 1e-9);
}

static void test_follow_t_0_5_goes_halfway(void) {
    camera c = {v2(0.0, 0.0), 1.0, 1280, 800};
    vec2 target = v2(100.0, 50.0);

    cam_follow(&c, target, 0.5);

    CHECK_NEAR(c.center.x, 50.0, 1e-9);
    CHECK_NEAR(c.center.y, 25.0, 1e-9);
}

static void test_zoom_at_different_factors(void) {
    camera c = {v2(10.0, 20.0), 2.0, 1280, 800};
    vec2 screen_pt = v2(640.0, 400.0);

    vec2 world_before = cam_to_world(&c, screen_pt);
    cam_zoom_at(&c, screen_pt, 0.5);
    vec2 world_after = cam_to_world(&c, screen_pt);

    CHECK_NEAR(world_after.x, world_before.x, 1e-9);
    CHECK_NEAR(world_after.y, world_before.y, 1e-9);
    CHECK_NEAR(c.scale, 1.0, 1e-9);
}

int main(void) {
    RUN(test_to_screen_world_roundtrip);
    RUN(test_world_origin_to_screen_centre);
    RUN(test_y_flip);
    RUN(test_zoom_at_keeps_point_fixed);
    RUN(test_zoom_clamps);
    RUN(test_pan);
    RUN(test_fit_uses_smaller_side);
    RUN(test_fit_wider_screen);
    RUN(test_follow_t_1_reaches_target);
    RUN(test_follow_t_0_5_goes_halfway);
    RUN(test_zoom_at_different_factors);
    return TEST_SUMMARY();
}
