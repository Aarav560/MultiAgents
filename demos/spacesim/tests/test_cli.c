#include "cli.h"
#include "config.h"
#include "scenario.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/* Builds an argv array from a NULL-terminated list of string literals, argv[0] = "orbit". */
#define MAKE_ARGV_NAMED(argv_name, argc_name, ...)                              \
    char *argv_name[] = {(char *)"orbit", __VA_ARGS__, NULL};                   \
    int argc_name = (int)(sizeof(argv_name) / sizeof(argv_name[0])) - 1

#define MAKE_ARGV(...) MAKE_ARGV_NAMED(argv_, argc_, __VA_ARGS__)

static void test_value_options_space_form(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--scenario", (char *)"binary", (char *)"--integrator", (char *)"rk4",
              (char *)"--gravity", (char *)"bh", (char *)"--theta", (char *)"0.3",
              (char *)"--dt", (char *)"1.5", (char *)"--duration", (char *)"100",
              (char *)"--steps", (char *)"50", (char *)"--every", (char *)"3",
              (char *)"--softening", (char *)"0.02", (char *)"--scale", (char *)"1000",
              (char *)"--csv", (char *)"out.csv", (char *)"--ppm", (char *)"frames",
              (char *)"--width", (char *)"640", (char *)"--height", (char *)"480",
              (char *)"--seed", (char *)"7");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == 0);
    CHECK(strcmp(cfg.scenario, "binary") == 0);
    CHECK(strcmp(cfg.integrator, "rk4") == 0);
    CHECK(strcmp(cfg.gravity, "bh") == 0);
    CHECK_NEAR(cfg.theta, 0.3, 1e-12);
    CHECK_NEAR(cfg.dt, 1.5, 1e-12);
    CHECK_NEAR(cfg.duration, 100.0, 1e-12);
    CHECK(cfg.steps == 50);
    CHECK(cfg.output_every == 3);
    CHECK_NEAR(cfg.softening, 0.02, 1e-12);
    CHECK_NEAR(cfg.scale, 1000.0, 1e-12);
    CHECK(strcmp(cfg.csv_path, "out.csv") == 0);
    CHECK(strcmp(cfg.ppm_dir, "frames") == 0);
    CHECK(cfg.width == 640);
    CHECK(cfg.height == 480);
    CHECK(cfg.seed == 7ULL);
}

static void test_value_options_equals_form(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--scenario=figure8", (char *)"--integrator=leapfrog",
              (char *)"--gravity=direct", (char *)"--theta=0.7", (char *)"--dt=0.5",
              (char *)"--duration=6.3", (char *)"--steps=10", (char *)"--every=2",
              (char *)"--softening=0.01", (char *)"--scale=2", (char *)"--csv=a.csv",
              (char *)"--ppm=p", (char *)"--width=100", (char *)"--height=50",
              (char *)"--seed=99");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == 0);
    CHECK(strcmp(cfg.scenario, "figure8") == 0);
    CHECK(strcmp(cfg.integrator, "leapfrog") == 0);
    CHECK(strcmp(cfg.gravity, "direct") == 0);
    CHECK_NEAR(cfg.theta, 0.7, 1e-12);
    CHECK_NEAR(cfg.dt, 0.5, 1e-12);
    CHECK_NEAR(cfg.duration, 6.3, 1e-12);
    CHECK(cfg.steps == 10);
    CHECK(cfg.output_every == 2);
    CHECK_NEAR(cfg.softening, 0.01, 1e-12);
    CHECK_NEAR(cfg.scale, 2.0, 1e-12);
    CHECK(strcmp(cfg.csv_path, "a.csv") == 0);
    CHECK(strcmp(cfg.ppm_dir, "p") == 0);
    CHECK(cfg.width == 100);
    CHECK(cfg.height == 50);
    CHECK(cfg.seed == 99ULL);
}

static void test_flags(void) {
    sim_config cfg;
    config_defaults(&cfg);
    CHECK(cfg.ascii == 0);
    CHECK(cfg.trails == 1);
    CHECK(cfg.collisions == 1);
    CHECK(cfg.quiet == 0);
    MAKE_ARGV((char *)"--ascii", (char *)"--no-trails", (char *)"--no-collisions",
              (char *)"--quiet");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == 0);
    CHECK(cfg.ascii == 1);
    CHECK(cfg.trails == 0);
    CHECK(cfg.collisions == 0);
    CHECK(cfg.quiet == 1);
}

static void test_flag_rejects_value(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--ascii=1");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == -1);
    CHECK(err[0] != '\0');
}

static void test_config_file_overridden_by_later_flags(void) {
    FILE *f = fopen("build/test_cli_config.ini", "w");
    CHECK(f != NULL);
    if (!f) {
        return;
    }
    fprintf(f, "scenario = binary\n");
    fprintf(f, "dt = 5\n");
    fprintf(f, "seed = 111\n");
    fclose(f);

    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--config", (char *)"build/test_cli_config.ini", (char *)"--dt", (char *)"9");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == 0);
    /* --config applied scenario/dt/seed, then --dt after it overrides dt only. */
    CHECK(strcmp(cfg.scenario, "binary") == 0);
    CHECK_NEAR(cfg.dt, 9.0, 1e-12);
    CHECK(cfg.seed == 111ULL);
}

static void test_options_before_config_are_overridden(void) {
    FILE *f = fopen("build/test_cli_config2.ini", "w");
    CHECK(f != NULL);
    if (!f) {
        return;
    }
    fprintf(f, "dt = 42\n");
    fclose(f);

    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--dt", (char *)"9", (char *)"--config", (char *)"build/test_cli_config2.ini");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == 0);
    CHECK_NEAR(cfg.dt, 42.0, 1e-12);
}

static void test_config_missing_file_errors(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--config", (char *)"build/does_not_exist.ini");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == -1);
    CHECK(err[0] != '\0');
}

static void test_unknown_option(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--bogus", (char *)"1");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == -1);
    CHECK(err[0] != '\0');
}

static void test_missing_value(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--dt");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == -1);
    CHECK(err[0] != '\0');
}

static void test_bad_value(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--dt", (char *)"not-a-number");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == -1);
    CHECK(err[0] != '\0');

    sim_config cfg2;
    config_defaults(&cfg2);
    MAKE_ARGV_NAMED(argv2_, argc2_, (char *)"--width", (char *)"-5");
    int rc2 = cli_parse(argc2_, argv2_, &cfg2, err, sizeof err);
    CHECK(rc2 == -1);
}

static void test_unexpected_positional_argument(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"loose");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == -1);
    CHECK(err[0] != '\0');
}

static void test_help_returns_1(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--help");
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    CHECK(rc == 1);

    sim_config cfg2;
    config_defaults(&cfg2);
    MAKE_ARGV_NAMED(argv2_, argc2_, (char *)"-h");
    int rc2 = cli_parse(argc2_, argv2_, &cfg2, err, sizeof err);
    CHECK(rc2 == 1);
}

static void test_list_returns_1_and_prints_scenarios(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--list");

    FILE *tmp = tmpfile();
    CHECK(tmp != NULL);
    if (!tmp) {
        return;
    }
    FILE *saved_stdout = stdout;
    stdout = tmp;
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    stdout = saved_stdout;
    CHECK(rc == 1);

    rewind(tmp);
    char line[128];
    int count = 0;
    int found_solar = 0;
    while (fgets(line, sizeof line, tmp)) {
        count++;
        if (strncmp(line, "solar", 5) == 0) {
            found_solar = 1;
        }
    }
    CHECK(count == scenario_count());
    CHECK(found_solar);
    fclose(tmp);
}

static void test_usage_to_tmpfile(void) {
    FILE *tmp = tmpfile();
    CHECK(tmp != NULL);
    if (!tmp) {
        return;
    }
    cli_usage(tmp, "orbit");
    long size = ftell(tmp);
    CHECK(size > 0);

    rewind(tmp);
    char buf[64];
    CHECK(fgets(buf, sizeof buf, tmp) != NULL);
    CHECK(strncmp(buf, "usage:", 6) == 0);
    fclose(tmp);
}

static void test_help_usage_redirected(void) {
    sim_config cfg;
    config_defaults(&cfg);
    MAKE_ARGV((char *)"--help");

    FILE *tmp = tmpfile();
    CHECK(tmp != NULL);
    if (!tmp) {
        return;
    }
    FILE *saved_stdout = stdout;
    stdout = tmp;
    char err[256] = {0};
    int rc = cli_parse(argc_, argv_, &cfg, err, sizeof err);
    stdout = saved_stdout;
    CHECK(rc == 1);

    long size = ftell(tmp);
    CHECK(size > 0);
    fclose(tmp);
}

int main(void) {
    RUN(test_value_options_space_form);
    RUN(test_value_options_equals_form);
    RUN(test_flags);
    RUN(test_flag_rejects_value);
    RUN(test_config_file_overridden_by_later_flags);
    RUN(test_options_before_config_are_overridden);
    RUN(test_config_missing_file_errors);
    RUN(test_unknown_option);
    RUN(test_missing_value);
    RUN(test_bad_value);
    RUN(test_unexpected_positional_argument);
    RUN(test_help_returns_1);
    RUN(test_list_returns_1_and_prints_scenarios);
    RUN(test_usage_to_tmpfile);
    RUN(test_help_usage_redirected);
    return TEST_SUMMARY();
}
