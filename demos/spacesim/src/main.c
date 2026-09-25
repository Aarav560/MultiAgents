#include "cli.h"
#include "config.h"
#include "sim.h"

#include <stdio.h>

int main(int argc, char **argv) {
    sim_config cfg;
    char err[512] = "";
    config_defaults(&cfg);
    int rc = cli_parse(argc, argv, &cfg, err, sizeof(err));
    if (rc == 1) return 0;
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        cli_usage(stderr, argv[0]);
        return 2;
    }
    simulation *s = sim_create(&cfg, err, sizeof(err));
    if (!s) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    rc = sim_run(s, stdout);
    sim_destroy(s);
    if (rc != 0) {
        fprintf(stderr, "%s: simulation failed\n", argv[0]);
        return 1;
    }
    return 0;
}
