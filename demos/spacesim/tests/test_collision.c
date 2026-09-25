#include "test.h"
#include "collision.h"
#include "world.h"
#include "body.h"
#include "rng.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- brute-force reference merge count, for cross-checking the grid path ---- */

static int brute_force_merge_count(const world *win) {
    world w;
    world_init(&w, win->count > 0 ? win->count : 1);
    for (int i = 0; i < win->count; i++) {
        body b = win->bodies[i];
        world_add(&w, &b);
    }

    int total = 0;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < w.count; i++) {
            if (!w.bodies[i].alive) continue;
            for (int j = i + 1; j < w.count; j++) {
                if (!w.bodies[j].alive) continue;
                double rsum = w.bodies[i].radius + w.bodies[j].radius;
                double d2 = vec3_len2(vec3_sub(w.bodies[i].pos, w.bodies[j].pos));
                if (d2 < rsum * rsum) {
                    body *bi = &w.bodies[i];
                    body *bj = &w.bodies[j];
                    double mi = bi->mass, mj = bj->mass, total_m = mi + mj;
                    int surv = (mj > mi) ? j : i;
                    int lose = (surv == i) ? j : i;
                    vec3 newpos = vec3_scale(vec3_add(vec3_scale(bi->pos, mi), vec3_scale(bj->pos, mj)), 1.0 / total_m);
                    vec3 newvel = vec3_scale(vec3_add(vec3_scale(bi->vel, mi), vec3_scale(bj->vel, mj)), 1.0 / total_m);
                    double newr = cbrt(bi->radius * bi->radius * bi->radius + bj->radius * bj->radius * bj->radius);
                    w.bodies[surv].mass = total_m;
                    w.bodies[surv].pos = newpos;
                    w.bodies[surv].vel = newvel;
                    w.bodies[surv].radius = newr;
                    w.bodies[lose].alive = 0;
                    total++;
                    changed = 1;
                    break;
                }
            }
            if (changed) break;
        }
    }

    world_free(&w);
    return total;
}

static void test_head_on_merge(void) {
    world w;
    world_init(&w, 4);

    body a = body_make("Alpha", BODY_PLANET, 3.0, 1.0, vec3_make(-0.5, 0, 0), vec3_make(1.0, 0, 0));
    body b = body_make("Beta", BODY_PLANET, 1.0, 1.0, vec3_make(0.5, 0, 0), vec3_make(-2.0, 0, 0));
    world_add(&w, &a);
    world_add(&w, &b);

    vec3 p0 = world_momentum(&w);
    double m0 = world_total_mass(&w);

    int merges = collision_merge(&w);
    CHECK(merges == 1);
    CHECK(world_alive(&w) == 1);

    /* find the surviving body */
    int idx = -1;
    for (int i = 0; i < w.count; i++) {
        if (w.bodies[i].alive) idx = i;
    }
    CHECK(idx >= 0);
    body *s = &w.bodies[idx];

    /* heavier body (Alpha, mass 3) survives with its name and id */
    CHECK(strcmp(s->name, "Alpha") == 0);

    double m1 = world_total_mass(&w);
    vec3 p1 = world_momentum(&w);
    CHECK_REL(m1, m0, 1e-12);
    CHECK_REL(p1.x, p0.x, 1e-12);
    CHECK_REL(p1.y, p0.y, 1e-12);
    CHECK_REL(p1.z, p0.z, 1e-12);

    /* expected mass-weighted position: (3*(-0.5) + 1*(0.5)) / 4 = -0.25 */
    CHECK_NEAR(s->pos.x, -0.25, 1e-12);
    CHECK_NEAR(s->pos.y, 0.0, 1e-12);
    CHECK_NEAR(s->pos.z, 0.0, 1e-12);

    /* radius = cbrt(1^3 + 1^3) = cbrt(2) */
    CHECK_NEAR(s->radius, cbrt(2.0), 1e-12);

    world_free(&w);
}

static void test_no_merge_when_touching(void) {
    world w;
    world_init(&w, 4);

    body a = body_make("A", BODY_ASTEROID, 1.0, 1.0, vec3_make(0, 0, 0), vec3_zero());
    body b = body_make("B", BODY_ASTEROID, 1.0, 1.0, vec3_make(2.0, 0, 0), vec3_zero()); /* dist == r1+r2 exactly */
    world_add(&w, &a);
    world_add(&w, &b);

    int merges = collision_merge(&w);
    CHECK(merges == 0);
    CHECK(world_alive(&w) == 2);

    world_free(&w);
}

static void test_nothing_to_do(void) {
    world w;
    world_init(&w, 4);
    int merges = collision_merge(&w);
    CHECK(merges == 0);

    body a = body_make("Only", BODY_PLANET, 1.0, 1.0, vec3_zero(), vec3_zero());
    world_add(&w, &a);
    merges = collision_merge(&w);
    CHECK(merges == 0);

    world_free(&w);
}

static void test_chain_merge(void) {
    world w;
    world_init(&w, 8);

    /* A and B overlap directly; the merged AB then overlaps C (which does
       not initially overlap A or B). */
    body a = body_make("A", BODY_PLANET, 1.0, 1.0, vec3_make(0.0, 0, 0), vec3_zero());
    body b = body_make("B", BODY_PLANET, 1.0, 1.0, vec3_make(1.5, 0, 0), vec3_zero());
    body c = body_make("C", BODY_PLANET, 1.0, 1.0, vec3_make(2.6, 0, 0), vec3_zero());
    world_add(&w, &a);
    world_add(&w, &b);
    world_add(&w, &c);

    /* sanity: C alone does not overlap A initially (dist 2.6 > 1+1=2) */
    CHECK(vec3_dist(a.pos, c.pos) > a.radius + c.radius);
    /* but merged AB sits at x=0.75, radius cbrt(2) ~ 1.26, so dist to C
       (1.85) < radius_sum (1.26+1) ~ 2.26, so it should chain-merge */

    double m0 = world_total_mass(&w);
    vec3 p0 = world_momentum(&w);

    int merges = collision_merge(&w);
    CHECK(merges == 2);
    CHECK(world_alive(&w) == 1);

    double m1 = world_total_mass(&w);
    vec3 p1 = world_momentum(&w);
    CHECK_REL(m1, m0, 1e-12);
    CHECK_REL(p1.x + 1.0, p0.x + 1.0, 1e-12); /* offset to avoid div-by-zero when momentum is 0 */

    world_free(&w);
}

static void test_grid_matches_brute_force(void) {
    rng r;
    rng_seed(&r, 42);

    world w;
    world_init(&w, 1000);
    for (int i = 0; i < 1000; i++) {
        vec3 pos = vec3_make(rng_range(&r, -50, 50), rng_range(&r, -50, 50), rng_range(&r, -50, 50));
        body b = body_make("p", BODY_PARTICLE, 1.0, 0.3, pos, vec3_zero());
        world_add(&w, &b);
    }

    int expected = brute_force_merge_count(&w);
    int actual = collision_merge(&w);
    CHECK(actual == expected);

    world_free(&w);
}

int main(void) {
    RUN(test_head_on_merge);
    RUN(test_no_merge_when_touching);
    RUN(test_nothing_to_do);
    RUN(test_chain_merge);
    RUN(test_grid_matches_brute_force);
    return TEST_SUMMARY();
}
