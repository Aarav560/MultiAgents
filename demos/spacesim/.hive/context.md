# Project context: `orbit`, an N-body space simulation in C11

## Goal
A fast, correct and well-tested gravitational N-body simulator: planets, moons, stars, galaxies,
spacecraft maneuvers, orbital mechanics, several integrators, direct and Barnes-Hut gravity,
collisions, diagnostics, CSV/ASCII/PPM output, a CLI and INI scenario files.

## Stack and conventions
- C11, no dependencies beyond libc and libm. Compiler flags (must compile clean):
  `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude` and link with `-lm`.
- Every module is `include/<m>.h` + `src/<m>.c` + `tests/test_<m>.c`. The header has an include guard
  `ORBIT_<M>_H`, includes only what it needs, and declares exactly the API below (you may add
  `static` helpers in the .c, never extra non-static globals).
- No global mutable state. No `exit()` or printing inside library code (only main.c, sim.c, cli.c print).
  Return `0` on success and a negative value on error unless the API says otherwise.
- Allocation failure returns an error and never crashes. Every `*_create/_open/_init` has a matching free.
- Units: SI (m, kg, s) unless a scenario sets `w->G = 1.0` (N-body units). Code must work in both.
- Style: 4-space indent, `snake_case`, braces on the same line, short functions, a comment only
  where the math is non-obvious (cite the formula: "Kepler's equation, Newton iteration").
- Tests use `tests/test.h` (`CHECK`, `CHECK_NEAR`, `CHECK_REL`, `RUN`, `TEST_SUMMARY`). Each test
  file has `int main(void)` returning `TEST_SUMMARY()`. Tests are deterministic, run in < 2 s and
  test real physics (conservation, known orbits, analytic results), not just "doesn't crash".
- A module's test compiles with ONLY its own source + `src/world.c` (modules are built in parallel):
  `mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_<m>.c src/<m>.c src/world.c -lm -o build/test_<m> && ./build/test_<m>`.
  If a test needs gravity, write a small static direct-sum helper inside the test file.

## Already written (the core; read it, don't modify it)
- `include/vec3.h`: `vec3 {x,y,z}` with inline `vec3_make zero add sub scale madd(a,b,s)=a+b*s dot cross len len2 dist norm lerp`.
- `include/body.h`: `body {name[32], kind, mass, radius, pos, vel, acc, color(rgb r,g,b), alive, id}`, `body_kind`
  (`BODY_STAR PLANET MOON ASTEROID SPACECRAFT PARTICLE`), `body_make(name, kind, mass, radius, pos, vel)`, `body_kind_name(k)`.
- `include/world.h` + `src/world.c`: `world {bodies, count, capacity, t, G, softening, step, next_id}`,
  `world_init(w, cap)`, `world_free`, `world_add(w, &b)` returning the index, `world_find(w, name)`, `world_compact`, `world_alive`,
  `world_total_mass`, `world_com`, `world_com_vel`, `world_momentum`, `world_recenter`, `world_extent`.
  Loops must skip bodies with `alive == 0`.
- `include/gravity.h`: `accel_fn` typedef, `gravity_direct`, `gravity_potential` (implementation src/gravity.c is a task).
- `include/rng.h`: `rng`, `rng_seed(&r, seed)`, `rng_u64`, `rng_double` [0,1), `rng_range(lo,hi)`, `rng_normal`.
- `include/config.h`: `sim_config` (all fields and defaults documented there), `config_defaults`, `config_set`, `config_load_file`.
- `tests/test.h`: the test harness.

## Module contracts (exact public API; implement your module's precisely)

### gravity.h (direct summation; header already written)
```c
typedef void (*accel_fn)(world *w, void *ctx);   /* fills acc of every alive body */
void gravity_direct(world *w, void *ctx);          /* ctx unused; O(n^2) pairwise, uses w->G and w->softening; zero acc for dead bodies */
double gravity_potential(const world *w);          /* -sum_{i<j} G m_i m_j / sqrt(r^2 + eps^2) */
```
Uses Newton's third law (visit each pair once). a_i = sum_j G m_j (r_j - r_i) / (r^2 + eps^2)^{3/2}.
The `accel_fn` typedef lives here; integrator.h and octree.h include gravity.h to use it.

### octree.h (Barnes-Hut)
```c
#include "gravity.h"
typedef struct octree octree;                         /* opaque */
typedef struct { double theta; } bh_params;           /* opening angle, typical 0.3-0.8 */
octree *octree_build(const world *w);                 /* over alive bodies; NULL on failure or no bodies */
void octree_free(octree *t);
int octree_node_count(const octree *t);
vec3 octree_accel_at(const octree *t, const world *w, vec3 pos, int skip_index, double theta);  /* skip_index = body to exclude, or -1 */
void gravity_barnes_hut(world *w, void *ctx);         /* accel_fn; ctx is bh_params* (NULL = theta 0.5) */
```
Node = cube with center of mass, total mass, 8 children, leaf body index. Use pooled node allocation (one array grown by doubling),
not one malloc per node. Handle coincident bodies (cap the depth at 64 and let the deepest leaf hold several). Opening criterion: size/dist < theta.
Softening as in gravity_direct. theta = 0 must reproduce direct summation to 1e-9 relative.

### integrator.h
```c
#include "gravity.h"
typedef enum { INT_EULER, INT_SYMPLECTIC_EULER, INT_LEAPFROG, INT_RK4, INT_YOSHIDA4 } integrator_kind;
int integrator_step(world *w, integrator_kind k, double dt, accel_fn accel, void *ctx);  /* one step; w->t += dt, w->step++ */
const char *integrator_name(integrator_kind k);      /* "euler" "symplectic" "leapfrog" "rk4" "yoshida" */
int integrator_parse(const char *name, integrator_kind *out);  /* 0 ok, -1 unknown */
int integrator_order(integrator_kind k);             /* 1 1 2 4 4 */
```
integrator_step calls `accel(w, ctx)` itself whenever it needs accelerations (never trust incoming acc).
Leapfrog is kick-drift-kick (velocity Verlet). Yoshida4 is the 4th-order symplectic composition with w1 = 1/(2-2^(1/3)), w0 = -2^(1/3) w1.
RK4 is classical RK4 on (pos, vel) for all bodies. It needs scratch arrays: malloc them per call and free before returning (-1 on OOM).
Dead bodies are left untouched.

### collision.h
```c
int collision_merge(world *w);   /* merges every pair of alive bodies with dist < r_i + r_j; returns merges done (>=0) or -1 */
```
Perfectly inelastic: mass and momentum conserved, position = mass-weighted mean, radius = cbrt(r1^3 + r2^3).
The heavier body survives (keeps its name, kind, color and id), and the other gets alive = 0. Does NOT compact.
Handle chains (A hits B, merged AB hits C) by repeating until no overlaps. Use a spatial hash grid when n > 64 (cell = 2 * max radius) and fall back to O(n^2) otherwise.

### diagnostics.h
```c
typedef struct { double kinetic, potential, total; vec3 momentum, angular_momentum, com; double virial_ratio; } diag;  /* virial = 2K/|U| */
void diagnostics_compute(const world *w, diag *out);   /* potential via its own O(n^2) loop (same formula as gravity_potential) */
double diagnostics_rel_drift(double e0, double e);     /* |e - e0| / max(|e0|, 1e-300) */
```

### orbit.h (Keplerian two-body mechanics)
```c
typedef struct { double a, e, i, raan, argp, nu; } orbit_elements;  /* m, -, rad, rad, rad, rad (true anomaly) */
int orbit_from_state(vec3 r, vec3 v, double mu, orbit_elements *out);   /* elliptic or hyperbolic; -1 on degenerate input */
int orbit_to_state(const orbit_elements *el, double mu, vec3 *r, vec3 *v);
double orbit_period(double a, double mu);               /* 2 pi sqrt(a^3/mu); returns 0 when a <= 0 */
double orbit_kepler_E(double M, double e);             /* solves M = E - e sin E, e < 1, Newton to 1e-14 */
double orbit_true_from_mean(double M, double e);
double orbit_vis_viva(double r, double a, double mu);  /* speed */
```
For circular or equatorial orbits (e < 1e-10 or i < 1e-10) the undefined angles are set to 0 and folded into nu. Round-trip error < 1e-8 relative.

### spacecraft.h (maneuvers)
```c
typedef struct { double t; vec3 dv; int body_id; int done; } burn;          /* body_id matches body.id */
typedef struct { burn *burns; int count, capacity; } flight_plan;
void flight_plan_init(flight_plan *p);
void flight_plan_free(flight_plan *p);
int flight_plan_add(flight_plan *p, double t, int body_id, vec3 dv);         /* keeps burns sorted by t */
int flight_plan_apply(flight_plan *p, world *w, double t0, double t1);      /* applies not-done burns with t0 <= t < t1; returns count */
vec3 spacecraft_prograde(const world *w, int ship_index, int central_index);/* unit vector along velocity relative to the central body */
typedef struct { double dv1, dv2, tof, a_transfer; } hohmann;
int hohmann_plan(double mu, double r1, double r2, hohmann *out);            /* classic two-impulse transfer between circular orbits */
double spacecraft_delta_v_budget(const flight_plan *p);                     /* sum of |dv| */
```

### scenario.h (built-in universes)
```c
typedef struct { double dt, duration, scale; char description[128]; } scenario_info;  /* suggested dt/duration (s), scale (m per pixel for 800px) */
int scenario_load(world *w, const char *name, unsigned long long seed, scenario_info *info);  /* w already initialized and empty; -1 unknown name */
int scenario_count(void);
const char *scenario_name(int index);
```
Scenarios (self-contained, including their own circular-velocity math, not using orbit.c). All end with `world_recenter`.
- `solar`: the Sun and 8 planets at J2000-like mean distances with circular orbits in their real inclinations (degrees; approximate), plus Earth's Moon. Real masses and radii, a distinct color each. dt 3600 s, duration 1 year.
- `earth-moon`: Earth and Moon (384,400 km); dt 60 s, duration 28 days.
- `binary`: two solar-mass stars, a = 1 AU, e = 0.5, plus a circumbinary planet at 4 AU. dt 3600, duration 3 years.
- `figure8`: G = 1, three unit masses, Chenciner-Montgomery initial conditions
  (x1 = -x2 = (0.97000436, -0.24308753), x3 = 0; v3 = (-0.93240737, -0.86473146), v1 = v2 = -v3/2). dt 0.001, duration 6.3259.
- `cluster`: G = 1, a Plummer sphere of 400 equal-mass bodies (total mass 1, scale radius 1) sampled with rng (seed), softening 0.05. dt 0.01, duration 10.
- `disk`: G = 1, central mass 1 (a star) plus 1500 test particles (mass 1e-7) on circular orbits, r uniform in [0.5, 3]. dt 0.005, duration 20, softening 0.01.
- `hohmann`: Earth plus a spacecraft "ship" in a circular 7000 km orbit (G_SI). dt 10 s, duration 6 h. (sim.c schedules the transfer to 42164 km.)

### output_csv.h
```c
typedef struct csv_writer csv_writer;
csv_writer *csv_open(const char *path);                 /* writes header: step,t,id,name,kind,mass,x,y,z,vx,vy,vz */
int csv_write_frame(csv_writer *c, const world *w);      /* one row per alive body, %.17g for doubles */
long csv_rows(const csv_writer *c);
int csv_close(csv_writer *c);                            /* flushes and frees; returns fclose status */
```
Names containing commas or quotes are quoted RFC-4180 style.

### render_ascii.h
```c
int render_ascii(const world *w, char *buf, size_t buflen, int cols, int rows, double scale); /* scale = m per cell, 0 = auto-fit */
```
Projects x-y centered on the center of mass. Writes rows lines of cols chars, each ending in '\n', NUL-terminated (needs rows*(cols+1)+1 bytes, else -1).
Glyph by kind: star '*', planet 'O', moon 'o', asteroid ':', spacecraft 'A', particle '.'. Heavier bodies win a shared cell. Draw a border with `+-|`. Auto-fit uses world_extent * 1.1.

### render_ppm.h
```c
typedef struct { int w, h; unsigned char *px; } image;        /* RGB, 3 bytes per pixel, row-major */
int image_init(image *im, int w, int h);                       /* black */
void image_free(image *im);
void image_fill(image *im, rgb c);
void image_fade(image *im, double keep);                       /* px *= keep (0..1): motion trails */
void image_plot(image *im, int x, int y, rgb c);               /* bounds-checked */
void image_disc(image *im, int cx, int cy, int r, rgb c);      /* filled circle, clipped */
void image_line(image *im, int x0, int y0, int x1, int y1, rgb c);  /* Bresenham */
int image_write_ppm(const image *im, const char *path);        /* binary P6 */
int render_world(image *im, const world *w, double scale);     /* x-y projection centered on COM; scale m/px (0 = auto); disc radius = max(1, radius/scale), capped at 12 px */
```

### cli.h
```c
#include "config.h"
#include <stdio.h>
int cli_parse(int argc, char **argv, sim_config *cfg, char *err, size_t errlen);  /* 0 ok, 1 = help/list printed, -1 error */
void cli_usage(FILE *out, const char *prog);
```
Options (long form, `--key value` or `--key=value`): `--scenario --integrator --gravity --theta --dt --duration --steps --every --softening --scale --csv --ppm --width --height --seed`,
flags `--ascii --no-trails --no-collisions --quiet`, `--config FILE` (applied immediately, so later flags override it), `--list` (prints scenario names, returns 1), `--help/-h` (returns 1).
It uses config_set for everything and maps flags to "1"/"0".

### sim.h (integration layer; the integrator task writes this)
```c
#include "config.h"
#include "world.h"
typedef struct simulation simulation;
simulation *sim_create(const sim_config *cfg, char *err, size_t errlen);
int sim_run(simulation *s, FILE *log);   /* runs to completion, writing outputs; returns 0 */
void sim_destroy(simulation *s);
const world *sim_world(const simulation *s);
```

## Layout (final)
```
include/  vec3.h body.h world.h rng.h config.h gravity.h octree.h integrator.h collision.h diagnostics.h
          orbit.h spacecraft.h scenario.h output_csv.h render_ascii.h render_ppm.h cli.h sim.h
src/      world.c gravity.c octree.c integrator.c collision.c diagnostics.c orbit.c spacecraft.c scenario.c
          config.c output_csv.c render_ascii.c render_ppm.c cli.c sim.c main.c
tests/    test.h test_<module>.c for every src module, test_core.c (vec3/rng/world), run_scenarios.sh
bench/    bench_gravity.c
scenarios/ <name>.ini for every scenario
docs/     physics.md usage.md
Makefile  README.md
```
