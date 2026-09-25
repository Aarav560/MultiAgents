#include "config.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

static void test_defaults(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(strcmp(c.scenario, "solar") == 0);
    CHECK(strcmp(c.integrator, "leapfrog") == 0);
    CHECK(strcmp(c.gravity, "direct") == 0);
    CHECK_NEAR(c.theta, 0.5, 1e-9);
    CHECK_NEAR(c.dt, 0.0, 1e-9);
    CHECK_NEAR(c.duration, 0.0, 1e-9);
    CHECK(c.steps == 0);
    CHECK(c.output_every == 0);
    CHECK_NEAR(c.softening, -1.0, 1e-9);
    CHECK_NEAR(c.scale, 0.0, 1e-9);
    CHECK(strcmp(c.csv_path, "") == 0);
    CHECK(strcmp(c.ppm_dir, "") == 0);
    CHECK(c.ascii == 0);
    CHECK(c.width == 800);
    CHECK(c.height == 800);
    CHECK(c.trails == 1);
    CHECK(c.collisions == 1);
    CHECK(c.quiet == 0);
    CHECK(c.seed == 42);
}

static void test_set_strings(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(config_set(&c, "scenario", "earth-moon") == 0);
    CHECK(strcmp(c.scenario, "earth-moon") == 0);

    CHECK(config_set(&c, "integrator", "rk4") == 0);
    CHECK(strcmp(c.integrator, "rk4") == 0);

    CHECK(config_set(&c, "gravity", "bh") == 0);
    CHECK(strcmp(c.gravity, "bh") == 0);

    CHECK(config_set(&c, "csv", "/tmp/output.csv") == 0);
    CHECK(strcmp(c.csv_path, "/tmp/output.csv") == 0);

    CHECK(config_set(&c, "ppm", "/tmp/frames") == 0);
    CHECK(strcmp(c.ppm_dir, "/tmp/frames") == 0);
}

static void test_set_doubles(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(config_set(&c, "theta", "0.8") == 0);
    CHECK_NEAR(c.theta, 0.8, 1e-9);

    CHECK(config_set(&c, "dt", "3600") == 0);
    CHECK_NEAR(c.dt, 3600.0, 1e-9);

    CHECK(config_set(&c, "duration", "1e6") == 0);
    CHECK_NEAR(c.duration, 1e6, 1e-6);

    CHECK(config_set(&c, "softening", "0.05") == 0);
    CHECK_NEAR(c.softening, 0.05, 1e-9);

    CHECK(config_set(&c, "scale", "1e8") == 0);
    CHECK_NEAR(c.scale, 1e8, 1e-9);
}

static void test_set_ints(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(config_set(&c, "steps", "1000") == 0);
    CHECK(c.steps == 1000);

    CHECK(config_set(&c, "every", "50") == 0);
    CHECK(c.output_every == 50);

    CHECK(config_set(&c, "width", "1024") == 0);
    CHECK(c.width == 1024);

    CHECK(config_set(&c, "height", "768") == 0);
    CHECK(c.height == 768);
}

static void test_set_seed(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(config_set(&c, "seed", "12345") == 0);
    CHECK(c.seed == 12345);

    CHECK(config_set(&c, "seed", "0") == 0);
    CHECK(c.seed == 0);

    /* Large unsigned long long */
    CHECK(config_set(&c, "seed", "18446744073709551615") == 0);
    CHECK(c.seed == 18446744073709551615ULL);
}

static void test_bool_true_spellings(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(config_set(&c, "ascii", "1") == 0);
    CHECK(c.ascii == 1);

    config_defaults(&c);
    CHECK(config_set(&c, "ascii", "true") == 0);
    CHECK(c.ascii == 1);

    config_defaults(&c);
    CHECK(config_set(&c, "ascii", "yes") == 0);
    CHECK(c.ascii == 1);

    config_defaults(&c);
    CHECK(config_set(&c, "ascii", "on") == 0);
    CHECK(c.ascii == 1);
}

static void test_bool_false_spellings(void) {
    sim_config c;
    config_defaults(&c);
    c.ascii = 1;  /* Start with true */

    CHECK(config_set(&c, "ascii", "0") == 0);
    CHECK(c.ascii == 0);

    c.ascii = 1;
    CHECK(config_set(&c, "ascii", "false") == 0);
    CHECK(c.ascii == 0);

    c.ascii = 1;
    CHECK(config_set(&c, "ascii", "no") == 0);
    CHECK(c.ascii == 0);

    c.ascii = 1;
    CHECK(config_set(&c, "ascii", "off") == 0);
    CHECK(c.ascii == 0);
}

static void test_invalid_values(void) {
    sim_config c;
    config_defaults(&c);
    double orig_theta = c.theta;

    /* Unparseable double */
    CHECK(config_set(&c, "theta", "not_a_number") == -2);
    CHECK_NEAR(c.theta, orig_theta, 1e-9);  /* Unchanged */

    /* Negative theta */
    CHECK(config_set(&c, "theta", "-0.5") == -2);
    CHECK_NEAR(c.theta, orig_theta, 1e-9);

    /* Negative dt */
    double orig_dt = c.dt;
    CHECK(config_set(&c, "dt", "-10") == -2);
    CHECK_NEAR(c.dt, orig_dt, 1e-9);

    /* Negative duration */
    double orig_dur = c.duration;
    CHECK(config_set(&c, "duration", "-1000") == -2);
    CHECK_NEAR(c.duration, orig_dur, 1e-9);

    /* Negative steps */
    long orig_steps = c.steps;
    CHECK(config_set(&c, "steps", "-1") == -2);
    CHECK(c.steps == orig_steps);

    /* Zero or negative width/height */
    int orig_width = c.width;
    CHECK(config_set(&c, "width", "0") == -2);
    CHECK(c.width == orig_width);

    CHECK(config_set(&c, "width", "-100") == -2);
    CHECK(c.width == orig_width);

    int orig_height = c.height;
    CHECK(config_set(&c, "height", "-50") == -2);
    CHECK(c.height == orig_height);

    /* Invalid boolean */
    int orig_ascii = c.ascii;
    CHECK(config_set(&c, "ascii", "maybe") == -2);
    CHECK(c.ascii == orig_ascii);

    /* String too long */
    char long_str[300];
    memset(long_str, 'x', sizeof long_str - 1);
    long_str[sizeof long_str - 1] = '\0';

    char orig_scenario[32];
    strcpy(orig_scenario, c.scenario);
    CHECK(config_set(&c, "scenario", long_str) == -2);
    CHECK(strcmp(c.scenario, orig_scenario) == 0);
}

static void test_unknown_key(void) {
    sim_config c;
    config_defaults(&c);

    CHECK(config_set(&c, "unknown_key", "some_value") == -1);
    CHECK(config_set(&c, "invalid", "123") == -1);
}

static void test_file_basic(void) {
    /* Create a test INI file */
    FILE *f = fopen("build/test_config.ini", "w");
    fprintf(f, "scenario = earth-moon\n");
    fprintf(f, "integrator = rk4\n");
    fprintf(f, "dt = 60\n");
    fprintf(f, "width = 1024\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    CHECK(config_load_file(&c, "build/test_config.ini", err, sizeof err) == 0);
    CHECK(strcmp(c.scenario, "earth-moon") == 0);
    CHECK(strcmp(c.integrator, "rk4") == 0);
    CHECK_NEAR(c.dt, 60.0, 1e-9);
    CHECK(c.width == 1024);
}

static void test_file_comments(void) {
    FILE *f = fopen("build/test_config_comments.ini", "w");
    fprintf(f, "# This is a comment\n");
    fprintf(f, "scenario = solar\n");
    fprintf(f, "; This is another comment\n");
    fprintf(f, "dt = 3600\n");
    fprintf(f, "\n");  /* blank line */
    fprintf(f, "gravity = direct\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    CHECK(config_load_file(&c, "build/test_config_comments.ini", err, sizeof err) == 0);
    CHECK(strcmp(c.scenario, "solar") == 0);
    CHECK_NEAR(c.dt, 3600.0, 1e-9);
    CHECK(strcmp(c.gravity, "direct") == 0);
}

static void test_file_sections(void) {
    FILE *f = fopen("build/test_config_sections.ini", "w");
    fprintf(f, "[General]\n");
    fprintf(f, "scenario = cluster\n");
    fprintf(f, "[Simulation]\n");
    fprintf(f, "integrator = yoshida\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    CHECK(config_load_file(&c, "build/test_config_sections.ini", err, sizeof err) == 0);
    CHECK(strcmp(c.scenario, "cluster") == 0);
    CHECK(strcmp(c.integrator, "yoshida") == 0);
}

static void test_file_whitespace(void) {
    FILE *f = fopen("build/test_config_whitespace.ini", "w");
    fprintf(f, "  scenario   =   figure8  \n");
    fprintf(f, "  integrator=euler  \n");
    fprintf(f, "dt=0.001\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    CHECK(config_load_file(&c, "build/test_config_whitespace.ini", err, sizeof err) == 0);
    CHECK(strcmp(c.scenario, "figure8") == 0);
    CHECK(strcmp(c.integrator, "euler") == 0);
    CHECK_NEAR(c.dt, 0.001, 1e-9);
}

static void test_file_crlf(void) {
    /* Write CRLF line endings */
    FILE *f = fopen("build/test_config_crlf.ini", "wb");
    fprintf(f, "scenario = binary\r\n");
    fprintf(f, "theta = 0.75\r\n");
    fprintf(f, "ascii = 1\r\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    CHECK(config_load_file(&c, "build/test_config_crlf.ini", err, sizeof err) == 0);
    CHECK(strcmp(c.scenario, "binary") == 0);
    CHECK_NEAR(c.theta, 0.75, 1e-9);
    CHECK(c.ascii == 1);
}

static void test_file_error_unknown_key(void) {
    FILE *f = fopen("build/test_config_bad_key.ini", "w");
    fprintf(f, "scenario = solar\n");
    fprintf(f, "invalid_key = value\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    int res = config_load_file(&c, "build/test_config_bad_key.ini", err, sizeof err);
    CHECK(res == -1);
    CHECK(strlen(err) > 0);
    CHECK(strstr(err, "invalid_key") != NULL);
    CHECK(strstr(err, ":2:") != NULL);  /* Line 2 */
}

static void test_file_error_invalid_value(void) {
    FILE *f = fopen("build/test_config_bad_value.ini", "w");
    fprintf(f, "scenario = solar\n");
    fprintf(f, "width = -100\n");
    fclose(f);

    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    int res = config_load_file(&c, "build/test_config_bad_value.ini", err, sizeof err);
    CHECK(res == -1);
    CHECK(strlen(err) > 0);
    CHECK(strstr(err, ":2:") != NULL);  /* Line 2 */
}

static void test_file_missing(void) {
    sim_config c;
    config_defaults(&c);

    char err[256] = "";
    int res = config_load_file(&c, "/tmp/nonexistent_config_file_xyz.ini", err, sizeof err);
    CHECK(res == -1);
    CHECK(strlen(err) > 0);
}

int main(void) {
    RUN(test_defaults);
    RUN(test_set_strings);
    RUN(test_set_doubles);
    RUN(test_set_ints);
    RUN(test_set_seed);
    RUN(test_bool_true_spellings);
    RUN(test_bool_false_spellings);
    RUN(test_invalid_values);
    RUN(test_unknown_key);
    RUN(test_file_basic);
    RUN(test_file_comments);
    RUN(test_file_sections);
    RUN(test_file_whitespace);
    RUN(test_file_crlf);
    RUN(test_file_error_unknown_key);
    RUN(test_file_error_invalid_value);
    RUN(test_file_missing);

    return TEST_SUMMARY();
}
