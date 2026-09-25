/* cli.h - command-line parsing into a sim_config. */
#ifndef ORBIT_CLI_H
#define ORBIT_CLI_H

#include "config.h"
#include <stdio.h>

/* Parses argv[1..argc) into *cfg (already filled with config_defaults, or previously loaded).
   Options are long form: "--key value" or "--key=value", mapped through config_set.
   Recognized options: --scenario --integrator --gravity --theta --dt --duration --steps --every
   --softening --scale --csv --ppm --width --height --seed.
   Flags (no value): --ascii --no-trails --no-collisions --quiet, mapped to config_set("ascii"/"trails"/
   "collisions"/"quiet", "1"/"0").
   --config FILE loads that INI file immediately via config_load_file, so options after --config on
   the command line override values it sets, and options before it are overridden by it.
   --list prints every scenario_name() to stdout, one per line, and returns 1.
   --help / -h prints usage via cli_usage(stdout, argv[0]) and returns 1.
   Returns 0 on success, 1 if help/list was printed (nothing else changed), -1 on error with a
   human-readable message written into err (if err and errlen > 0). */
int cli_parse(int argc, char **argv, sim_config *cfg, char *err, size_t errlen);

/* Prints a usage summary listing every option to `out`. */
void cli_usage(FILE *out, const char *prog);
/* Prints one scenario name per line (what --list shows). */
void cli_list_scenarios(FILE *out);

#endif
