#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "vec3.h"
#include "body.h"
#include "world.h"
#include "gravity.h"
#include "octree.h"
#include "rng.h"

/* Generate Plummer sphere position via inverse sampling */
static vec3 plummer_position(rng *r, double scale) {
    /* Radial CDF inverse: r = scale * sqrt(u^(-2/3) - 1) */
    double u = rng_double(r);
    double rad = scale * sqrt(1.0 / (u * u * u * u) - 1.0);

    /* Random direction on sphere */
    double theta = acos(2.0 * rng_double(r) - 1.0);
    double phi = 2.0 * M_PI * rng_double(r);

    return vec3_make(
        rad * sin(theta) * cos(phi),
        rad * sin(theta) * sin(phi),
        rad * cos(theta)
    );
}

/* Generate velocity from isotropic distribution */
static vec3 plummer_velocity(rng *r, double scale, double total_mass) {
    /* Velocity scale from virial theorem: v ~ sqrt(G*M/r_scale) */
    double v_scale = sqrt(total_mass / scale);

    return vec3_make(
        v_scale * rng_normal(r),
        v_scale * rng_normal(r),
        v_scale * rng_normal(r)
    );
}

int main(int argc, char **argv) {
    int n = 5000;
    if (argc > 1) {
        n = atoi(argv[1]);
        if (n < 1) n = 5000;
    }

    /* Initialize world */
    world w;
    if (world_init(&w, n + 10) != 0) {
        fprintf(stderr, "Failed to initialize world\n");
        return -1;
    }

    /* Set N-body parameters */
    w.G = 1.0;
    w.softening = 0.01;

    /* Generate Plummer sphere */
    rng r;
    rng_seed(&r, 42);  /* deterministic seed */

    double total_mass = 1.0;
    double mass_per_body = total_mass / n;

    for (int i = 0; i < n; i++) {
        body b = body_make(NULL, BODY_PARTICLE, mass_per_body, 0.0,
                          plummer_position(&r, 1.0),
                          plummer_velocity(&r, 1.0, total_mass));
        if (world_add(&w, &b) < 0) {
            fprintf(stderr, "Failed to add body %d\n", i);
            world_free(&w);
            return -1;
        }
    }

    /* Recenter to COM frame */
    world_recenter(&w);

    /* Allocate storage for reference accelerations */
    vec3 *acc_direct = malloc(sizeof(vec3) * w.count);
    if (!acc_direct) {
        fprintf(stderr, "Failed to allocate memory\n");
        world_free(&w);
        return -1;
    }

    /* Compute reference direct gravity and time it */
    struct timespec t0, t1;
    int iterations = 5;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int iter = 0; iter < iterations; iter++) {
        gravity_direct(&w, NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double time_direct = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                         (t1.tv_nsec - t0.tv_nsec) / 1e6;
    double ms_per_direct = time_direct / iterations;

    /* Store direct accelerations */
    for (int i = 0; i < w.count; i++) {
        acc_direct[i] = w.bodies[i].acc;
    }

    /* Benchmark Barnes-Hut with different theta values */
    double thetas[] = {0.3, 0.5, 0.8};
    int num_thetas = sizeof(thetas) / sizeof(thetas[0]);

    printf("Gravity benchmark (n=%d, softening=%.2f, G=%.1f)\n", n, w.softening, w.G);
    printf("%-18s %12s %12s %15s\n", "Method", "ms/eval", "Speedup", "RMS rel error");
    printf("%-18s %12.3f %12.1f %15s\n", "Direct", ms_per_direct, 1.0, "baseline");

    for (int theta_idx = 0; theta_idx < num_thetas; theta_idx++) {
        double theta = thetas[theta_idx];
        bh_params params = {theta};

        /* Reset accelerations */
        for (int i = 0; i < w.count; i++) {
            w.bodies[i].acc = vec3_zero();
        }

        /* Time Barnes-Hut */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int iter = 0; iter < iterations; iter++) {
            gravity_barnes_hut(&w, &params);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);

        double time_bh = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                         (t1.tv_nsec - t0.tv_nsec) / 1e6;
        double ms_per_bh = time_bh / iterations;
        double speedup = ms_per_direct / ms_per_bh;

        /* Compute RMS relative error vs direct */
        double sum_sq_error = 0.0;
        double sum_sq_ref = 0.0;

        for (int i = 0; i < w.count; i++) {
            if (w.bodies[i].alive) {
                vec3 err = vec3_sub(w.bodies[i].acc, acc_direct[i]);
                double err_sq = vec3_len2(err);
                double ref_sq = vec3_len2(acc_direct[i]);

                sum_sq_error += err_sq;
                sum_sq_ref += ref_sq;
            }
        }

        double rms_rel_error = 0.0;
        if (sum_sq_ref > 0.0) {
            rms_rel_error = sqrt(sum_sq_error / sum_sq_ref);
        }

        printf("BH (theta=%.1f)    %12.3f %12.2f %15.3e\n", theta, ms_per_bh, speedup, rms_rel_error);
    }

    free(acc_direct);
    world_free(&w);
    return 0;
}
