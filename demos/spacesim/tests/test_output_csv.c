#include "test.h"

#include "output_csv.h"
#include "world.h"
#include "body.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_csv_basic(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    body b = body_make("Earth", BODY_PLANET, 5.972e24, 6.371e6, vec3_make(1e11, 2e11, 3e11),
                       vec3_make(1e4, 2e4, 3e4));
    CHECK(world_add(&w, &b) == 0);

    csv_writer *c = csv_open("build/test.csv");
    CHECK(c != NULL);

    CHECK(csv_rows(c) == 0);
    CHECK(csv_write_frame(c, &w) == 0);
    CHECK(csv_rows(c) == 1);

    CHECK(csv_close(c) == 0);

    /* Verify file was written and contains expected header */
    FILE *f = fopen("build/test.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL);
    CHECK(strcmp(line, "step,t,id,name,kind,mass,x,y,z,vx,vy,vz\n") == 0);

    ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL);
    /* Verify it contains the body id 0 and name Earth */
    CHECK(strstr(line, ",0,Earth,planet,") != NULL);

    fclose(f);
    world_free(&w);
}

static void test_csv_dead_bodies_skipped(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    body b1 = body_make("Alive", BODY_PLANET, 1e24, 1e6, vec3_make(0, 0, 0), vec3_make(0, 0, 0));
    body b2 = body_make("Dead", BODY_PLANET, 2e24, 1e6, vec3_make(1e6, 0, 0), vec3_make(0, 0, 0));

    CHECK(world_add(&w, &b1) == 0);
    CHECK(world_add(&w, &b2) == 1);

    /* Kill the second body */
    w.bodies[1].alive = 0;

    csv_writer *c = csv_open("build/test_dead.csv");
    CHECK(c != NULL);

    CHECK(csv_write_frame(c, &w) == 0);
    CHECK(csv_rows(c) == 1); /* Only one alive body */

    CHECK(csv_close(c) == 0);

    /* Verify only one data row */
    FILE *f = fopen("build/test_dead.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* header */
    ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* first body */
    CHECK(strstr(line, "Alive") != NULL);

    int lines = 1;
    while (fgets(line, sizeof line, f)) lines++;
    CHECK(lines == 1); /* No second data row */

    fclose(f);
    world_free(&w);
}

static void test_csv_name_escaping(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    /* Name with comma and quote */
    body b = body_make("Name,with\"quote", BODY_PLANET, 1e24, 1e6, vec3_make(0, 0, 0),
                       vec3_make(0, 0, 0));
    CHECK(world_add(&w, &b) == 0);

    csv_writer *c = csv_open("build/test_escape.csv");
    CHECK(c != NULL);

    CHECK(csv_write_frame(c, &w) == 0);
    CHECK(csv_close(c) == 0);

    /* Verify escaping: the name should be quoted and internal quotes doubled */
    FILE *f = fopen("build/test_escape.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* header */
    ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* data row */

    /* Expected: id,name part should be: ,0,"Name,with""quote",
       The quote is doubled in the escaped field */
    CHECK(strstr(line, "\"Name,with\"\"quote\"") != NULL);

    fclose(f);
    world_free(&w);
}

static void test_csv_double_precision(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    /* Use a double that tests %.17g precision */
    double val = 1.23456789012345e-100;
    body b = body_make("test", BODY_PARTICLE, val, val, vec3_make(val, val, val),
                       vec3_make(val, val, val));
    CHECK(world_add(&w, &b) == 0);

    csv_writer *c = csv_open("build/test_precision.csv");
    CHECK(c != NULL);

    CHECK(csv_write_frame(c, &w) == 0);
    CHECK(csv_close(c) == 0);

    /* Verify the value round-trips correctly via %.17g */
    FILE *f = fopen("build/test_precision.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* header */
    ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* data row */

    /* Parse back the mass field (6th field) and verify it round-trips */
    char *p = line;
    for (int i = 0; i < 5; i++) {
        p = strchr(p, ',');
        CHECK(p != NULL);
        p++;
    }
    char mass_str[64];
    sscanf(p, "%63[^,]", mass_str);
    double mass_read = strtod(mass_str, NULL);

    /* The value should round-trip exactly */
    CHECK_REL(mass_read, val, 1e-17);

    fclose(f);
    world_free(&w);
}

static void test_csv_multiple_frames(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    body b = body_make("moving", BODY_ASTEROID, 1e10, 1e3, vec3_make(0, 0, 0), vec3_make(1, 2, 3));
    CHECK(world_add(&w, &b) == 0);

    csv_writer *c = csv_open("build/test_frames.csv");
    CHECK(c != NULL);

    /* Write multiple frames with different times and steps */
    for (int i = 0; i < 3; i++) {
        w.step = (unsigned long long)i;
        w.t = (double)i * 10.5;
        CHECK(csv_write_frame(c, &w) == 0);
    }

    CHECK(csv_rows(c) == 3);
    CHECK(csv_close(c) == 0);

    /* Verify three data rows with different t values */
    FILE *f = fopen("build/test_frames.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* header */

    for (int i = 0; i < 3; i++) {
        ret = fgets(line, sizeof line, f);
        CHECK(ret != NULL);
        char t_str[64];
        sscanf(line, "%*[^,],%63[^,]", t_str);
        double t_read = strtod(t_str, NULL);
        CHECK_NEAR(t_read, (double)i * 10.5, 1e-14);
    }

    fclose(f);
    world_free(&w);
}

static void test_csv_empty_name(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    /* Body with empty name */
    body b = body_make("", BODY_PARTICLE, 1e10, 1e3, vec3_make(1, 2, 3), vec3_make(4, 5, 6));
    CHECK(world_add(&w, &b) == 0);

    csv_writer *c = csv_open("build/test_empty_name.csv");
    CHECK(c != NULL);

    CHECK(csv_write_frame(c, &w) == 0);
    CHECK(csv_rows(c) == 1);
    CHECK(csv_close(c) == 0);

    FILE *f = fopen("build/test_empty_name.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* header */
    ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* data row with empty name */

    /* Should have consecutive commas for empty name: ,0,,particle, */
    CHECK(strstr(line, ",0,,particle,") != NULL);

    fclose(f);
    world_free(&w);
}

static void test_csv_special_doubles(void) {
    world w;
    CHECK(world_init(&w, 10) == 0);

    /* Test special double values */
    body b = body_make("special", BODY_PARTICLE, 0.0, 1.0, vec3_make(-1.5e100, 2.5e-100, 0.0),
                       vec3_make(1e-308, 1e308, -0.0));
    CHECK(world_add(&w, &b) == 0);

    csv_writer *c = csv_open("build/test_special_doubles.csv");
    CHECK(c != NULL);

    CHECK(csv_write_frame(c, &w) == 0);
    CHECK(csv_close(c) == 0);

    /* Just verify file was created and contains data */
    FILE *f = fopen("build/test_special_doubles.csv", "r");
    CHECK(f != NULL);

    char line[512];
    char *ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* header */
    ret = fgets(line, sizeof line, f);
    CHECK(ret != NULL); /* data row */

    /* Check that we have content and the kind is particle */
    CHECK(strstr(line, "particle") != NULL);

    fclose(f);
    world_free(&w);
}

int main(void) {
    RUN(test_csv_basic);
    RUN(test_csv_dead_bodies_skipped);
    RUN(test_csv_name_escaping);
    RUN(test_csv_double_precision);
    RUN(test_csv_multiple_frames);
    RUN(test_csv_empty_name);
    RUN(test_csv_special_doubles);
    return TEST_SUMMARY();
}
