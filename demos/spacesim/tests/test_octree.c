/* test_octree.c - Barnes-Hut tree against an exact direct sum. */
#include "octree.h"
#include "rng.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Exact pairwise accelerations with Plummer softening (reference). */
static void direct_sum(const world *w, vec3 *out) {
    double eps2 = w->softening * w->softening;
    for (int i = 0; i < w->count; i++) {
        out[i] = vec3_zero();
        if (!w->bodies[i].alive) continue;
        for (int j = 0; j < w->count; j++) {
            if (j == i || !w->bodies[j].alive) continue;
            vec3 d = vec3_sub(w->bodies[j].pos, w->bodies[i].pos);
            double r2 = vec3_len2(d) + eps2;
            if (r2 <= 0.0) continue;
            out[i] = vec3_madd(out[i], d, w->G * w->bodies[j].mass / (r2 * sqrt(r2)));
        }
    }
}

static void make_random(world *w, int n, unsigned long long seed) {
    rng r;
    rng_seed(&r, seed);
    world_init(w, n);
    w->G = 1.0;
    w->softening = 0.01;
    for (int i = 0; i < n; i++) {
        vec3 p = vec3_make(rng_range(&r, -1, 1), rng_range(&r, -1, 1), rng_range(&r, -1, 1));
        body b = body_make("p", BODY_PARTICLE, rng_range(&r, 0.5, 1.5), 0.0, p, vec3_zero());
        world_add(w, &b);
    }
}

static double rel_err(vec3 a, vec3 ref) { return vec3_len(vec3_sub(a, ref)) / vec3_len(ref); }

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void test_theta_zero_exact(void) {
    world w;
    make_random(&w, 300, 7);
    vec3 *ref = malloc(300 * sizeof *ref);
    direct_sum(&w, ref);
    bh_params p = {0.0};
    gravity_barnes_hut(&w, &p);
    double worst = 0.0;
    for (int i = 0; i < 300; i++) {
        double e = rel_err(w.bodies[i].acc, ref[i]);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-9);
    free(ref);
    world_free(&w);
}

static void test_theta_half_accuracy(void) {
    world w;
    make_random(&w, 300, 7);
    vec3 *ref = malloc(300 * sizeof *ref);
    double errs[300];
    direct_sum(&w, ref);
    bh_params p = {0.5};
    gravity_barnes_hut(&w, &p);
    for (int i = 0; i < 300; i++) errs[i] = rel_err(w.bodies[i].acc, ref[i]);
    qsort(errs, 300, sizeof errs[0], cmp_double);
    CHECK(errs[150] < 0.01);
    CHECK(errs[150] > 0.0); /* really approximating */
    /* NULL ctx means theta 0.5: identical result. */
    vec3 a0 = w.bodies[17].acc;
    gravity_barnes_hut(&w, NULL);
    CHECK(vec3_len(vec3_sub(a0, w.bodies[17].acc)) == 0.0);
    free(ref);
    world_free(&w);
}

static void test_node_count(void) {
    world w;
    make_random(&w, 300, 7);
    octree *t = octree_build(&w);
    CHECK(t != NULL);
    CHECK(octree_node_count(t) > 300);
    octree_free(t);
    world_free(&w);
}

static void test_coincident(void) {
    world w;
    world_init(&w, 8);
    w.G = 1.0;
    w.softening = 0.0;
    for (int i = 0; i < 100; i++) {
        body b = body_make("c", BODY_PARTICLE, 1.0, 0.0, vec3_make(0.25, 0.25, 0.25), vec3_zero());
        world_add(&w, &b);
    }
    body far = body_make("far", BODY_PARTICLE, 1.0, 0.0, vec3_make(10.25, 0.25, 0.25), vec3_zero());
    world_add(&w, &far);
    octree *t = octree_build(&w);
    CHECK(t != NULL);
    CHECK(octree_node_count(t) <= 66 * 8);
    octree_free(t);
    bh_params p = {0.0};
    gravity_barnes_hut(&w, &p);
    CHECK_REL(w.bodies[100].acc.x, -1.0, 1e-12); /* 100 / 10^2 */
    CHECK_REL(w.bodies[0].acc.x, 0.01, 1e-12);   /* coincident partners add nothing */
    CHECK(isfinite(w.bodies[0].acc.y) && isfinite(w.bodies[0].acc.z));
    world_free(&w);
}

static void test_single_body(void) {
    world w;
    world_init(&w, 4);
    body b = body_make("sun", BODY_STAR, 2e30, 7e8, vec3_make(1e11, 2e11, 3e11), vec3_zero());
    world_add(&w, &b);
    w.bodies[0].acc = vec3_make(1, 2, 3);
    gravity_barnes_hut(&w, NULL);
    CHECK(vec3_len(w.bodies[0].acc) == 0.0);
    octree *t = octree_build(&w);
    CHECK(t != NULL && octree_node_count(t) == 1);
    /* The body's field at a distance point: G M / r^2. */
    vec3 q = vec3_make(1e11 + 1e10, 2e11, 3e11);
    CHECK_REL(octree_accel_at(t, &w, q, -1, 0.5).x, -G_SI * 2e30 / 1e20, 1e-12);
    octree_free(t);
    world_free(&w);
}

static void test_dead_ignored(void) {
    world w;
    make_random(&w, 60, 11);
    for (int i = 0; i < 60; i += 3) {
        w.bodies[i].alive = 0;
        w.bodies[i].mass = 1e6;
        w.bodies[i].acc = vec3_make(5, 5, 5);
    }
    vec3 ref[60];
    direct_sum(&w, ref);
    bh_params p = {0.0};
    gravity_barnes_hut(&w, &p);
    int ok = 1;
    for (int i = 0; i < 60; i++) {
        if (!w.bodies[i].alive) ok &= vec3_len(w.bodies[i].acc) == 0.0;
        else ok &= rel_err(w.bodies[i].acc, ref[i]) < 1e-9;
    }
    CHECK(ok);
    for (int i = 0; i < 60; i++) w.bodies[i].alive = 0;
    CHECK(octree_build(&w) == NULL);
    world_free(&w);
}

static void test_deterministic(void) {
    world w;
    make_random(&w, 300, 7);
    octree *a = octree_build(&w), *b = octree_build(&w);
    CHECK(octree_node_count(a) == octree_node_count(b));
    int same = 1;
    for (int i = 0; i < 300; i++) {
        vec3 x = octree_accel_at(a, &w, w.bodies[i].pos, i, 0.6);
        vec3 y = octree_accel_at(b, &w, w.bodies[i].pos, i, 0.6);
        same &= memcmp(&x, &y, sizeof x) == 0;
    }
    CHECK(same);
    octree_free(a);
    octree_free(b);
    world_free(&w);
}

int main(void) {
    RUN(test_theta_zero_exact);
    RUN(test_theta_half_accuracy);
    RUN(test_node_count);
    RUN(test_coincident);
    RUN(test_single_body);
    RUN(test_dead_ignored);
    RUN(test_deterministic);
    return TEST_SUMMARY();
}
