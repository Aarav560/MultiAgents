#include <stdlib.h>
#include <time.h>

#include "sim.h"
#include "test.h"

static unsigned long long lcg_state;

static double rnd(void) {
    lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(lcg_state >> 11) / 9007199254740992.0;
}

static void fill_random(world *w, int n, unsigned long long seed) {
    lcg_state = seed;
    for (int i = 0; i < n; i++) {
        double r = 10.0 * sqrt(rnd()), a = 6.283185307179586 * rnd();
        body b = body_make(NULL, KIND_DUST, 0.5 + rnd(), 0.01, v2(r * cos(a), r * sin(a)),
                           v2(0.0, 0.0), 0xffffff);
        world_add(w, &b);
    }
}

static void direct_ref(const world *w, vec2 *out) {
    double eps2 = w->soft * w->soft;
    for (int i = 0; i < w->n; i++) {
        out[i] = v2(0.0, 0.0);
        if (!w->b[i].alive) continue;
        for (int j = 0; j < w->n; j++) {
            if (j == i || !w->b[j].alive) continue;
            vec2 d = v2_sub(w->b[j].pos, w->b[i].pos);
            double r2 = v2_len2(d);
            if (r2 == 0.0) continue;
            r2 += eps2;
            out[i] = v2_madd(out[i], d, w->G * w->b[j].mass / (r2 * sqrt(r2)));
        }
    }
}

static double rel_err(vec2 a, vec2 ref) { return v2_len(v2_sub(a, ref)) / v2_len(ref); }

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void test_theta_zero_exact(void) {
    world w;
    world_init(&w, 2000);
    w.G = 2.5;
    w.soft = 0.01;
    fill_random(&w, 2000, 1);
    vec2 *ref = malloc(2000 * sizeof *ref);
    direct_ref(&w, ref);
    gravity_bh(&w, 0.0);
    double worst = 0.0;
    for (int i = 0; i < w.n; i++) {
        double e = rel_err(w.b[i].acc, ref[i]);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-9);
    free(ref);
    world_free(&w);
}

static void test_theta_half_accuracy(void) {
    world w;
    world_init(&w, 2000);
    w.soft = 0.01;
    fill_random(&w, 2000, 7);
    vec2 *ref = malloc(2000 * sizeof *ref);
    double *err = malloc(2000 * sizeof *err);
    direct_ref(&w, ref);
    gravity_bh(&w, 0.5);
    for (int i = 0; i < w.n; i++) err[i] = rel_err(w.b[i].acc, ref[i]);
    qsort(err, 2000, sizeof *err, cmp_double);
    CHECK(err[1000] < 0.01);
    CHECK(err[1000] > 0.0);  /* actually approximating */
    free(err);
    free(ref);
    world_free(&w);
}

static void test_dead_ignored(void) {
    world w;
    world_init(&w, 16);
    body a = body_make("a", KIND_STAR, 1.0, 0.1, v2(0.0, 0.0), v2(0.0, 0.0), 0);
    body b = body_make("b", KIND_STAR, 2.0, 0.1, v2(2.0, 0.0), v2(0.0, 0.0), 0);
    body c = body_make("c", KIND_STAR, 100.0, 0.1, v2(0.0, 1.0), v2(0.0, 0.0), 0);
    world_add(&w, &a);
    world_add(&w, &b);
    world_add(&w, &c);
    w.b[2].alive = 0;
    w.b[2].acc = v2(5.0, 5.0);
    gravity_bh(&w, 0.5);
    CHECK_NEAR(w.b[0].acc.x, 0.5, 1e-12);
    CHECK_NEAR(w.b[0].acc.y, 0.0, 1e-12);
    CHECK_NEAR(w.b[1].acc.x, -0.25, 1e-12);
    CHECK_NEAR(w.b[2].acc.x, 0.0, 0.0);
    CHECK_NEAR(w.b[2].acc.y, 0.0, 0.0);
    world_free(&w);
}

static void test_single_body(void) {
    world w;
    world_init(&w, 16);
    body a = body_make("a", KIND_STAR, 1.0, 0.1, v2(3.0, 4.0), v2(0.0, 0.0), 0);
    world_add(&w, &a);
    w.b[0].acc = v2(1.0, 1.0);
    gravity_bh(&w, 0.5);
    CHECK_NEAR(w.b[0].acc.x, 0.0, 0.0);
    CHECK_NEAR(w.b[0].acc.y, 0.0, 0.0);
    world_free(&w);
}

static void test_coincident(void) {
    world w;
    world_init(&w, 128);
    for (int i = 0; i < 100; i++) {
        body a = body_make(NULL, KIND_DUST, 1.0, 0.1, v2(1.0, 1.0), v2(0.0, 0.0), 0);
        world_add(&w, &a);
    }
    body far = body_make(NULL, KIND_DUST, 1.0, 0.1, v2(4.0, 5.0), v2(0.0, 0.0), 0);
    world_add(&w, &far);
    gravity_bh(&w, 0.7);
    int finite = 1;
    for (int i = 0; i < w.n; i++)
        if (!isfinite(w.b[i].acc.x) || !isfinite(w.b[i].acc.y)) finite = 0;
    CHECK(finite);
    /* the far body feels 100 unit masses at distance 5 */
    CHECK_REL(v2_len(w.b[100].acc), 100.0 / 25.0, 1e-9);
    world_free(&w);
}

static void test_timing(void) {
    world w;
    world_init(&w, 10000);
    w.soft = 0.01;
    fill_random(&w, 10000, 42);
    gravity_bh(&w, 0.7); /* warm the pools */
    int reps = 5;
    clock_t t0 = clock();
    for (int r = 0; r < reps; r++) gravity_bh(&w, 0.7);
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / reps;
    fprintf(stderr, "  gravity_bh: 10000 bodies, theta 0.7: %.2f ms/call\n", ms);
    CHECK(ms < 250.0);
    world_free(&w);
}

int main(void) {
    RUN(test_theta_zero_exact);
    RUN(test_theta_half_accuracy);
    RUN(test_dead_ignored);
    RUN(test_single_body);
    RUN(test_coincident);
    RUN(test_timing);
    return TEST_SUMMARY();
}
