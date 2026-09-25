#include "test.h"
#include "vec3.h"
#include "rng.h"
#include "world.h"
#include "body.h"
#include <stdlib.h>
#include <string.h>

/* ===== vec3 tests ===== */

static void test_vec3_basic(void) {
    vec3 a = vec3_make(1.0, 2.0, 3.0);
    vec3 b = vec3_make(2.0, 3.0, 4.0);

    /* Addition */
    vec3 c = vec3_add(a, b);
    CHECK(fabs(c.x - 3.0) < 1e-14);
    CHECK(fabs(c.y - 5.0) < 1e-14);
    CHECK(fabs(c.z - 7.0) < 1e-14);

    /* Subtraction */
    c = vec3_sub(b, a);
    CHECK(fabs(c.x - 1.0) < 1e-14);
    CHECK(fabs(c.y - 1.0) < 1e-14);
    CHECK(fabs(c.z - 1.0) < 1e-14);

    /* Scaling */
    c = vec3_scale(a, 2.0);
    CHECK(fabs(c.x - 2.0) < 1e-14);
    CHECK(fabs(c.y - 4.0) < 1e-14);
    CHECK(fabs(c.z - 6.0) < 1e-14);
}

static void test_vec3_dot(void) {
    vec3 a = vec3_make(1.0, 0.0, 0.0);
    vec3 b = vec3_make(0.0, 1.0, 0.0);
    vec3 c = vec3_make(1.0, 2.0, 3.0);
    vec3 d = vec3_make(2.0, 3.0, 4.0);

    /* Orthogonal vectors have dot product 0 */
    CHECK(fabs(vec3_dot(a, b)) < 1e-14);

    /* Dot product: 1*2 + 2*3 + 3*4 = 2 + 6 + 12 = 20 */
    CHECK(fabs(vec3_dot(c, d) - 20.0) < 1e-14);

    /* Self dot is length squared */
    CHECK(fabs(vec3_dot(a, a) - 1.0) < 1e-14);
}

static void test_vec3_cross(void) {
    vec3 x = vec3_make(1.0, 0.0, 0.0);
    vec3 y = vec3_make(0.0, 1.0, 0.0);

    /* x cross y = z */
    vec3 c = vec3_cross(x, y);
    CHECK(fabs(c.x - 0.0) < 1e-14);
    CHECK(fabs(c.y - 0.0) < 1e-14);
    CHECK(fabs(c.z - 1.0) < 1e-14);

    /* y cross x = -z (anticommutativity) */
    c = vec3_cross(y, x);
    CHECK(fabs(c.x - 0.0) < 1e-14);
    CHECK(fabs(c.y - 0.0) < 1e-14);
    CHECK(fabs(c.z + 1.0) < 1e-14);

    /* General anticommutativity: cross(a,b) = -cross(b,a) */
    vec3 a = vec3_make(1.0, 2.0, 3.0);
    vec3 b = vec3_make(4.0, 5.0, 6.0);
    vec3 ab = vec3_cross(a, b);
    vec3 ba = vec3_cross(b, a);
    CHECK(fabs(ab.x + ba.x) < 1e-14);
    CHECK(fabs(ab.y + ba.y) < 1e-14);
    CHECK(fabs(ab.z + ba.z) < 1e-14);

    /* A vector cross itself is zero */
    c = vec3_cross(a, a);
    CHECK(fabs(c.x) < 1e-14);
    CHECK(fabs(c.y) < 1e-14);
    CHECK(fabs(c.z) < 1e-14);
}

static void test_vec3_length(void) {
    vec3 a = vec3_make(3.0, 4.0, 0.0);
    /* 3-4-5 triangle: length = 5 */
    CHECK(fabs(vec3_len(a) - 5.0) < 1e-14);
    CHECK(fabs(vec3_len2(a) - 25.0) < 1e-14);

    vec3 zero = vec3_zero();
    CHECK(fabs(vec3_len(zero)) < 1e-14);
    CHECK(fabs(vec3_len2(zero)) < 1e-14);
}

static void test_vec3_distance(void) {
    vec3 a = vec3_make(0.0, 0.0, 0.0);
    vec3 b = vec3_make(3.0, 4.0, 0.0);
    CHECK(fabs(vec3_dist(a, b) - 5.0) < 1e-14);
    CHECK(fabs(vec3_dist(a, a)) < 1e-14);
}

static void test_vec3_norm(void) {
    /* Norm of unit vector should be unit vector */
    vec3 x = vec3_make(1.0, 0.0, 0.0);
    vec3 nx = vec3_norm(x);
    CHECK(fabs(nx.x - 1.0) < 1e-14);
    CHECK(fabs(nx.y) < 1e-14);
    CHECK(fabs(nx.z) < 1e-14);

    /* Norm of arbitrary vector should have length 1 */
    vec3 a = vec3_make(3.0, 4.0, 0.0);
    vec3 na = vec3_norm(a);
    double len = vec3_len(na);
    CHECK_NEAR(len, 1.0, 1e-14);

    /* Norm of zero vector should be zero vector */
    vec3 zero = vec3_zero();
    vec3 nz = vec3_norm(zero);
    CHECK(fabs(nz.x) < 1e-14);
    CHECK(fabs(nz.y) < 1e-14);
    CHECK(fabs(nz.z) < 1e-14);
}

static void test_vec3_madd(void) {
    /* madd(a, b, s) = a + b*s */
    vec3 a = vec3_make(1.0, 2.0, 3.0);
    vec3 b = vec3_make(4.0, 5.0, 6.0);
    vec3 c = vec3_madd(a, b, 2.0);
    CHECK(fabs(c.x - 9.0) < 1e-14);   /* 1 + 4*2 */
    CHECK(fabs(c.y - 12.0) < 1e-14);  /* 2 + 5*2 */
    CHECK(fabs(c.z - 15.0) < 1e-14);  /* 3 + 6*2 */
}

static void test_vec3_lerp(void) {
    /* Linear interpolation */
    vec3 a = vec3_make(0.0, 0.0, 0.0);
    vec3 b = vec3_make(10.0, 10.0, 10.0);

    /* t=0 should give a */
    vec3 c = vec3_lerp(a, b, 0.0);
    CHECK(fabs(c.x) < 1e-14);
    CHECK(fabs(c.y) < 1e-14);
    CHECK(fabs(c.z) < 1e-14);

    /* t=1 should give b */
    c = vec3_lerp(a, b, 1.0);
    CHECK(fabs(c.x - 10.0) < 1e-14);
    CHECK(fabs(c.y - 10.0) < 1e-14);
    CHECK(fabs(c.z - 10.0) < 1e-14);

    /* t=0.5 should give midpoint */
    c = vec3_lerp(a, b, 0.5);
    CHECK(fabs(c.x - 5.0) < 1e-14);
    CHECK(fabs(c.y - 5.0) < 1e-14);
    CHECK(fabs(c.z - 5.0) < 1e-14);
}

/* ===== rng tests ===== */

static void test_rng_determinism(void) {
    rng r1, r2;
    rng_seed(&r1, 12345);
    rng_seed(&r2, 12345);

    /* Same seed should produce same sequence */
    for (int i = 0; i < 100; i++) {
        uint64_t u1 = rng_u64(&r1);
        uint64_t u2 = rng_u64(&r2);
        CHECK(u1 == u2);
    }

    /* Different seeds should (usually) produce different sequences */
    rng r3;
    rng_seed(&r3, 54321);
    rng_seed(&r1, 12345); /* Reset r1 */
    uint64_t u1 = rng_u64(&r1);
    uint64_t u3 = rng_u64(&r3);
    CHECK(u1 != u3); /* Very high probability */
}

static void test_rng_double_range(void) {
    rng r;
    rng_seed(&r, 999);

    /* rng_double should be in [0, 1) */
    for (int i = 0; i < 1000; i++) {
        double d = rng_double(&r);
        CHECK(d >= 0.0 && d < 1.0);
    }

    /* rng_range should be in [lo, hi) */
    rng_seed(&r, 888);
    double lo = 10.0, hi = 20.0;
    for (int i = 0; i < 1000; i++) {
        double d = rng_range(&r, lo, hi);
        CHECK(d >= lo && d < hi);
    }
}

static void test_rng_double_mean(void) {
    rng r;
    rng_seed(&r, 424242);

    /* Mean of many uniform samples should be near 0.5 */
    double sum = 0.0;
    int n = 100000;
    for (int i = 0; i < n; i++) {
        sum += rng_double(&r);
    }
    double mean = sum / n;
    CHECK_NEAR(mean, 0.5, 0.01); /* Allow ±1% */
}

static void test_rng_normal_stats(void) {
    rng r;
    rng_seed(&r, 31415);

    int n = 100000;
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        double x = rng_normal(&r);
        sum += x;
        sum_sq += x * x;
    }
    double mean = sum / n;
    double variance = sum_sq / n - mean * mean;

    /* Mean should be near 0 */
    CHECK_NEAR(mean, 0.0, 0.02);
    /* Variance should be near 1 */
    CHECK_NEAR(variance, 1.0, 0.05);
}

/* ===== world tests ===== */

static void test_world_init_free(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);
    CHECK(w.capacity >= 10);
    CHECK(w.count == 0);
    CHECK(w.G == G_SI);
    CHECK(w.softening == 0.0);
    CHECK(w.next_id == 0);
    world_free(&w);
    CHECK(w.bodies == NULL);
}

static void test_world_add_basic(void) {
    world w;
    world_init(&w, 2);

    body b1 = body_make("A", BODY_PLANET, 1e24, 1e7, vec3_make(1, 0, 0), vec3_make(0, 1, 0));
    int idx1 = world_add(&w, &b1);
    CHECK(idx1 == 0);
    CHECK(w.count == 1);
    CHECK(w.bodies[0].id == 0);
    CHECK(strcmp(w.bodies[0].name, "A") == 0);

    body b2 = body_make("B", BODY_PLANET, 2e24, 1e7, vec3_make(2, 0, 0), vec3_make(0, 1, 0));
    int idx2 = world_add(&w, &b2);
    CHECK(idx2 == 1);
    CHECK(w.count == 2);
    CHECK(w.bodies[1].id == 1);
    CHECK(strcmp(w.bodies[1].name, "B") == 0);

    world_free(&w);
}

static void test_world_add_growth(void) {
    world w;
    world_init(&w, 2); /* Start small */
    int initial_capacity = w.capacity;

    /* Add more bodies than capacity */
    for (int i = 0; i < 10; i++) {
        body b = body_make("test", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_zero());
        int idx = world_add(&w, &b);
        CHECK(idx == i);
        CHECK(w.count == i + 1);
        CHECK(w.bodies[i].id == i);
    }

    /* Capacity should have grown */
    CHECK(w.capacity > initial_capacity);
    CHECK(w.capacity >= 10);

    world_free(&w);
}

static void test_world_add_stable_ids(void) {
    world w;
    world_init(&w, 5);

    /* Add bodies with interspersed removals */
    body b1 = body_make("B1", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_zero());
    int id1 = world_add(&w, &b1);
    CHECK(id1 == 0);

    body b2 = body_make("B2", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_zero());
    int id2 = world_add(&w, &b2);
    CHECK(id2 == 1);

    body b3 = body_make("B3", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_zero());
    int id3 = world_add(&w, &b3);
    CHECK(id3 == 2);

    /* IDs should be stable and sequential */
    CHECK(w.bodies[0].id == 0);
    CHECK(w.bodies[1].id == 1);
    CHECK(w.bodies[2].id == 2);
    CHECK(w.next_id == 3);

    world_free(&w);
}

static void test_world_find_alive(void) {
    world w;
    world_init(&w, 10);

    body b1 = body_make("Earth", BODY_PLANET, 1.0, 1.0, vec3_zero(), vec3_zero());
    body b2 = body_make("Moon", BODY_MOON, 0.1, 1.0, vec3_make(1, 0, 0), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);

    /* Find alive bodies */
    body *found = world_find(&w, "Earth");
    CHECK(found != NULL);
    CHECK(found->alive == 1);
    CHECK(strcmp(found->name, "Earth") == 0);

    found = world_find(&w, "Moon");
    CHECK(found != NULL);
    CHECK(strcmp(found->name, "Moon") == 0);

    /* Mark Moon as dead */
    w.bodies[1].alive = 0;

    /* Should not find dead body */
    found = world_find(&w, "Moon");
    CHECK(found == NULL);

    /* Should still find alive body */
    found = world_find(&w, "Earth");
    CHECK(found != NULL);

    /* Should not find non-existent body */
    found = world_find(&w, "Sun");
    CHECK(found == NULL);

    world_free(&w);
}

static void test_world_compact(void) {
    world w;
    world_init(&w, 10);

    /* Add 5 bodies */
    for (int i = 0; i < 5; i++) {
        body b = body_make("test", BODY_PARTICLE, 1.0, 1.0, vec3_make(i, 0, 0), vec3_zero());
        world_add(&w, &b);
    }
    CHECK(w.count == 5);

    /* Kill some bodies */
    w.bodies[1].alive = 0;
    w.bodies[3].alive = 0;

    /* Compact */
    int removed = world_compact(&w);
    CHECK(removed == 2);
    CHECK(w.count == 3);

    /* Check order is preserved (only alive bodies remain) */
    CHECK(fabs(w.bodies[0].pos.x - 0.0) < 1e-14);
    CHECK(fabs(w.bodies[1].pos.x - 2.0) < 1e-14);
    CHECK(fabs(w.bodies[2].pos.x - 4.0) < 1e-14);

    world_free(&w);
}

static void test_world_total_mass(void) {
    world w;
    world_init(&w, 10);

    body b1 = body_make("A", BODY_PARTICLE, 5.0, 1.0, vec3_zero(), vec3_zero());
    body b2 = body_make("B", BODY_PARTICLE, 3.0, 1.0, vec3_zero(), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);

    double m = world_total_mass(&w);
    CHECK(fabs(m - 8.0) < 1e-14);

    /* Kill one body */
    w.bodies[1].alive = 0;
    m = world_total_mass(&w);
    CHECK(fabs(m - 5.0) < 1e-14);

    world_free(&w);
}

static void test_world_com(void) {
    world w;
    world_init(&w, 10);

    /* Two equal masses at (0,0,0) and (2,0,0): COM at (1,0,0) */
    body b1 = body_make("A", BODY_PARTICLE, 1.0, 1.0, vec3_make(0, 0, 0), vec3_zero());
    body b2 = body_make("B", BODY_PARTICLE, 1.0, 1.0, vec3_make(2, 0, 0), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);

    vec3 com = world_com(&w);
    CHECK(fabs(com.x - 1.0) < 1e-14);
    CHECK(fabs(com.y) < 1e-14);
    CHECK(fabs(com.z) < 1e-14);

    /* Dead bodies should not affect COM */
    w.bodies[1].alive = 0;
    com = world_com(&w);
    CHECK(fabs(com.x - 0.0) < 1e-14);

    /* Empty world */
    world_compact(&w);
    com = world_com(&w);
    CHECK(fabs(com.x) < 1e-14 && fabs(com.y) < 1e-14 && fabs(com.z) < 1e-14);

    world_free(&w);
}

static void test_world_momentum(void) {
    world w;
    world_init(&w, 10);

    /* Two bodies with equal and opposite momentum */
    body b1 = body_make("A", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_make(1, 0, 0));
    body b2 = body_make("B", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_make(-1, 0, 0));
    world_add(&w, &b1);
    world_add(&w, &b2);

    vec3 p = world_momentum(&w);
    CHECK(fabs(p.x) < 1e-14);
    CHECK(fabs(p.y) < 1e-14);
    CHECK(fabs(p.z) < 1e-14);

    /* Dead bodies should not count */
    w.bodies[1].alive = 0;
    p = world_momentum(&w);
    CHECK(fabs(p.x - 1.0) < 1e-14);

    world_free(&w);
}

static void test_world_com_vel(void) {
    world w;
    world_init(&w, 10);

    /* Two equal masses with different velocities */
    body b1 = body_make("A", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_make(2, 0, 0));
    body b2 = body_make("B", BODY_PARTICLE, 1.0, 1.0, vec3_zero(), vec3_make(4, 0, 0));
    world_add(&w, &b1);
    world_add(&w, &b2);

    vec3 v = world_com_vel(&w);
    CHECK(fabs(v.x - 3.0) < 1e-14); /* (2+4)/2 = 3 */
    CHECK(fabs(v.y) < 1e-14);
    CHECK(fabs(v.z) < 1e-14);

    world_free(&w);
}

static void test_world_recenter(void) {
    world w;
    world_init(&w, 10);

    /* Two bodies: one at (1,0,0) moving at (1,0,0), one at (3,0,0) moving at (3,0,0) */
    body b1 = body_make("A", BODY_PARTICLE, 1.0, 1.0, vec3_make(1, 0, 0), vec3_make(1, 0, 0));
    body b2 = body_make("B", BODY_PARTICLE, 1.0, 1.0, vec3_make(3, 0, 0), vec3_make(3, 0, 0));
    world_add(&w, &b1);
    world_add(&w, &b2);

    world_recenter(&w);

    /* After recentering, COM should be at origin with zero velocity */
    vec3 com = world_com(&w);
    vec3 com_vel = world_com_vel(&w);
    CHECK(fabs(com.x) < 1e-14 && fabs(com.y) < 1e-14 && fabs(com.z) < 1e-14);
    CHECK(fabs(com_vel.x) < 1e-14 && fabs(com_vel.y) < 1e-14 && fabs(com_vel.z) < 1e-14);

    world_free(&w);
}

static void test_world_extent(void) {
    world w;
    world_init(&w, 10);

    /* Three bodies at different distances from the center */
    body b1 = body_make("A", BODY_PARTICLE, 1.0, 1.0, vec3_make(1, 0, 0), vec3_zero());
    body b2 = body_make("B", BODY_PARTICLE, 1.0, 1.0, vec3_make(0, 2, 0), vec3_zero());
    body b3 = body_make("C", BODY_PARTICLE, 1.0, 1.0, vec3_make(0, 0, 3), vec3_zero());
    world_add(&w, &b1);
    world_add(&w, &b2);
    world_add(&w, &b3);

    /* COM is at (1/3, 2/3, 1) */
    double extent = world_extent(&w);

    /* Maximum distance should be one of the bodies' distances from COM */
    /* This is a sanity check that extent returns something reasonable */
    CHECK(extent > 0.0);

    /* Dead bodies should not affect extent */
    w.bodies[0].alive = 0;
    double extent2 = world_extent(&w);
    CHECK(extent2 > 0.0);
    CHECK(extent2 != extent); /* Should change after removing a body */

    world_free(&w);
}

static void test_body_make_truncation(void) {
    /* Test that long names are truncated */
    char long_name[256];
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    body b = body_make(long_name, BODY_PLANET, 1.0, 1.0, vec3_zero(), vec3_zero());

    /* Name should be at most BODY_NAME_MAX - 1 characters (+ NUL) */
    size_t name_len = strlen(b.name);
    CHECK(name_len == BODY_NAME_MAX - 1);
    CHECK(b.name[BODY_NAME_MAX - 1] == '\0');

    /* First BODY_NAME_MAX-1 characters should match */
    for (size_t i = 0; i < BODY_NAME_MAX - 1; i++) {
        CHECK(b.name[i] == 'A');
    }
}

static void test_body_make_defaults(void) {
    body b = body_make("TestBody", BODY_SPACECRAFT, 1000.0, 10.0, vec3_make(1, 2, 3), vec3_make(4, 5, 6));

    CHECK(strcmp(b.name, "TestBody") == 0);
    CHECK(b.kind == BODY_SPACECRAFT);
    CHECK(fabs(b.mass - 1000.0) < 1e-14);
    CHECK(fabs(b.radius - 10.0) < 1e-14);
    CHECK(fabs(b.pos.x - 1.0) < 1e-14);
    CHECK(fabs(b.pos.y - 2.0) < 1e-14);
    CHECK(fabs(b.pos.z - 3.0) < 1e-14);
    CHECK(fabs(b.vel.x - 4.0) < 1e-14);
    CHECK(fabs(b.vel.y - 5.0) < 1e-14);
    CHECK(fabs(b.vel.z - 6.0) < 1e-14);
    CHECK(fabs(b.acc.x) < 1e-14 && fabs(b.acc.y) < 1e-14 && fabs(b.acc.z) < 1e-14);
    CHECK(b.color.r == 255 && b.color.g == 255 && b.color.b == 255);
    CHECK(b.alive == 1);
    CHECK(b.id == -1);
}

static void test_body_make_null_name(void) {
    body b = body_make(NULL, BODY_PLANET, 1.0, 1.0, vec3_zero(), vec3_zero());
    CHECK(b.name[0] == '\0'); /* Empty name */
}

int main(void) {
    /* vec3 tests */
    RUN(test_vec3_basic);
    RUN(test_vec3_dot);
    RUN(test_vec3_cross);
    RUN(test_vec3_length);
    RUN(test_vec3_distance);
    RUN(test_vec3_norm);
    RUN(test_vec3_madd);
    RUN(test_vec3_lerp);

    /* rng tests */
    RUN(test_rng_determinism);
    RUN(test_rng_double_range);
    RUN(test_rng_double_mean);
    RUN(test_rng_normal_stats);

    /* world tests */
    RUN(test_world_init_free);
    RUN(test_world_add_basic);
    RUN(test_world_add_growth);
    RUN(test_world_add_stable_ids);
    RUN(test_world_find_alive);
    RUN(test_world_compact);
    RUN(test_world_total_mass);
    RUN(test_world_com);
    RUN(test_world_momentum);
    RUN(test_world_com_vel);
    RUN(test_world_recenter);
    RUN(test_world_extent);
    RUN(test_body_make_truncation);
    RUN(test_body_make_defaults);
    RUN(test_body_make_null_name);

    return TEST_SUMMARY();
}
