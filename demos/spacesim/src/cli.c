/* cli.c - command-line parsing into a sim_config. */
#include "cli.h"
#include "scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errlen, const char *fmt, const char *a) {
    if (!err || errlen == 0) {
        return;
    }
    if (a) {
        snprintf(err, errlen, fmt, a);
    } else {
        snprintf(err, errlen, "%s", fmt);
    }
}

/* Options that take a value, mapped to their config_set key. */
typedef struct {
    const char *opt;
    const char *key;
} opt_map;

static const opt_map value_opts[] = {
    {"scenario", "scenario"},
    {"integrator", "integrator"},
    {"gravity", "gravity"},
    {"theta", "theta"},
    {"dt", "dt"},
    {"duration", "duration"},
    {"steps", "steps"},
    {"every", "every"},
    {"softening", "softening"},
    {"scale", "scale"},
    {"csv", "csv"},
    {"ppm", "ppm"},
    {"width", "width"},
    {"height", "height"},
    {"seed", "seed"},
};
static const size_t n_value_opts = sizeof(value_opts) / sizeof(value_opts[0]);

/* Flags (no value), mapped to a config key and the value they set. */
typedef struct {
    const char *opt;
    const char *key;
    const char *value;
} flag_map;

static const flag_map flag_opts[] = {
    {"ascii", "ascii", "1"},
    {"no-trails", "trails", "0"},
    {"no-collisions", "collisions", "0"},
    {"quiet", "quiet", "1"},
};
static const size_t n_flag_opts = sizeof(flag_opts) / sizeof(flag_opts[0]);

void cli_usage(FILE *out, const char *prog) {
    fprintf(out, "usage: %s [options]\n", prog ? prog : "orbit");
    fprintf(out, "options:\n");
    fprintf(out, "  --scenario NAME     scenario to run (see --list)\n");
    fprintf(out, "  --integrator NAME   euler|symplectic|leapfrog|rk4|yoshida\n");
    fprintf(out, "  --gravity NAME      direct|bh\n");
    fprintf(out, "  --theta N           Barnes-Hut opening angle\n");
    fprintf(out, "  --dt N              integration step, seconds\n");
    fprintf(out, "  --duration N        run length, seconds\n");
    fprintf(out, "  --steps N           run exactly N steps (overrides duration)\n");
    fprintf(out, "  --every N           output every N steps\n");
    fprintf(out, "  --softening N       gravitational softening length\n");
    fprintf(out, "  --scale N           render scale, metres per pixel/cell\n");
    fprintf(out, "  --csv PATH          write a CSV trajectory to PATH\n");
    fprintf(out, "  --ppm DIR           write PPM frames to DIR\n");
    fprintf(out, "  --width N           image width, pixels\n");
    fprintf(out, "  --height N          image height, pixels\n");
    fprintf(out, "  --seed N            RNG seed\n");
    fprintf(out, "  --ascii             draw the system in the terminal\n");
    fprintf(out, "  --no-trails         disable image motion trails\n");
    fprintf(out, "  --no-collisions     disable collision merging\n");
    fprintf(out, "  --quiet             suppress the per-output status line\n");
    fprintf(out, "  --config FILE       load options from an INI file\n");
    fprintf(out, "  --list              list scenario names and exit\n");
    fprintf(out, "  --help, -h          show this help and exit\n");
}

static void list_scenarios(void) {
    int n = scenario_count();
    for (int i = 0; i < n; i++) {
        printf("%s\n", scenario_name(i));
    }
}

/* Splits "--key=value" into key/value (both pointing inside a mutable copy of arg).
   Returns 1 if an '=' was found, 0 otherwise. */
static int split_eq(char *arg, char **key_out, char **val_out) {
    char *eq = strchr(arg, '=');
    if (!eq) {
        *key_out = arg;
        *val_out = NULL;
        return 0;
    }
    *eq = '\0';
    *key_out = arg;
    *val_out = eq + 1;
    return 1;
}

int cli_parse(int argc, char **argv, sim_config *cfg, char *err, size_t errlen) {
    if (!argv || !cfg) {
        set_err(err, errlen, "internal error: null argument", NULL);
        return -1;
    }
    const char *prog = argc > 0 ? argv[0] : "orbit";

    for (int i = 1; i < argc; i++) {
        char raw[512];
        size_t len = strlen(argv[i]);
        if (len >= sizeof(raw)) {
            set_err(err, errlen, "option too long: '%s'", argv[i]);
            return -1;
        }
        memcpy(raw, argv[i], len + 1);

        if (strcmp(raw, "-h") == 0 || strcmp(raw, "--help") == 0) {
            cli_usage(stdout, prog);
            return 1;
        }
        if (strcmp(raw, "--list") == 0) {
            list_scenarios();
            return 1;
        }

        if (strncmp(raw, "--", 2) != 0) {
            set_err(err, errlen, "unexpected argument '%s'", argv[i]);
            return -1;
        }
        char *body = raw + 2;
        if (*body == '\0') {
            set_err(err, errlen, "unexpected argument '%s'", argv[i]);
            return -1;
        }

        char *key, *inline_val;
        int had_eq = split_eq(body, &key, &inline_val);

        if (strcmp(key, "config") == 0) {
            const char *val = inline_val;
            if (!val) {
                if (i + 1 >= argc) {
                    set_err(err, errlen, "missing value for '--config'", NULL);
                    return -1;
                }
                val = argv[++i];
            }
            char loaderr[256];
            if (config_load_file(cfg, val, loaderr, sizeof(loaderr)) != 0) {
                set_err(err, errlen, "%s", loaderr);
                return -1;
            }
            continue;
        }

        int matched = 0;

        for (size_t f = 0; f < n_flag_opts; f++) {
            if (strcmp(key, flag_opts[f].opt) == 0) {
                if (had_eq) {
                    set_err(err, errlen, "'--%s' takes no value", flag_opts[f].opt);
                    return -1;
                }
                int rc = config_set(cfg, flag_opts[f].key, flag_opts[f].value);
                if (rc != 0) {
                    set_err(err, errlen, "invalid value for '--%s'", flag_opts[f].opt);
                    return -1;
                }
                matched = 1;
                break;
            }
        }
        if (matched) {
            continue;
        }

        for (size_t v = 0; v < n_value_opts; v++) {
            if (strcmp(key, value_opts[v].opt) == 0) {
                const char *val = inline_val;
                if (!val) {
                    if (i + 1 >= argc) {
                        set_err(err, errlen, "missing value for '--%s'", value_opts[v].opt);
                        return -1;
                    }
                    val = argv[++i];
                }
                int rc = config_set(cfg, value_opts[v].key, val);
                if (rc != 0) {
                    set_err(err, errlen, "invalid value for '--%s'", value_opts[v].opt);
                    return -1;
                }
                matched = 1;
                break;
            }
        }
        if (matched) {
            continue;
        }

        set_err(err, errlen, "unknown option '--%s'", key);
        return -1;
    }

    return 0;
}
