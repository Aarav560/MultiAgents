/* config.h - run configuration, filled from defaults, an INI file and the command line. */
#ifndef ORBIT_CONFIG_H
#define ORBIT_CONFIG_H

#include <stddef.h>

typedef struct {
    char scenario[32];     /* key "scenario"    default "solar" */
    char integrator[16];   /* key "integrator"  default "leapfrog" (euler|symplectic|leapfrog|rk4|yoshida) */
    char gravity[16];      /* key "gravity"     default "direct"   (direct|bh) */
    double theta;          /* key "theta"       default 0.5  Barnes-Hut opening angle */
    double dt;             /* key "dt"          default 0    0 = scenario's suggested dt (s) */
    double duration;       /* key "duration"    default 0    0 = scenario's suggested duration (s) */
    long steps;            /* key "steps"       default 0    >0 overrides duration: run exactly this many steps */
    long output_every;     /* key "every"       default 0    0 = auto (about 200 frames per run) */
    double softening;      /* key "softening"   default -1   <0 = keep the scenario's softening */
    double scale;          /* key "scale"       default 0    render scale, metres per pixel/cell; 0 = auto */
    char csv_path[256];    /* key "csv"         default ""   empty = no CSV */
    char ppm_dir[256];     /* key "ppm"         default ""   empty = no images; else frames written as DIR/frame_000123.ppm */
    int ascii;             /* key "ascii"       default 0    1 = draw the system in the terminal every output step */
    double fps;            /* key "fps"         default 20   terminal animation speed with ascii; 0 = as fast as possible */
    int width, height;     /* keys "width" "height" default 800 800 (images); ascii uses 100x40 */
    int trails;            /* key "trails"      default 1    image trails on/off */
    int collisions;        /* key "collisions"  default 1    merge colliding bodies */
    int quiet;             /* key "quiet"       default 0    suppress the per-output status line */
    unsigned long long seed; /* key "seed"      default 42 */
} sim_config;

/* Fills every field with the defaults documented above. */
void config_defaults(sim_config *c);

/* Sets one field from text. Keys as documented above; booleans accept 1/0/true/false/yes/no/on/off.
   Returns 0 on success, -1 for an unknown key, -2 for an invalid value (unparseable, negative
   dt/duration/steps/theta, zero or negative width/height, string too long). */
int config_set(sim_config *c, const char *key, const char *value);

/* Loads an INI-style file: "key = value" lines, '#' or ';' comments, blank lines, optional
   "[section]" headers which are ignored, whitespace trimmed. Returns 0, or -1 and writes a message
   such as "scenarios/x.ini:7: unknown key 'foo'" into err (if err != NULL). */
int config_load_file(sim_config *c, const char *path, char *err, size_t errlen);

#endif
