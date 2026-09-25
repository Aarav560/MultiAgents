goal: orbit - an N-body space simulation in C11 (gravity, Barnes-Hut, 5 integrators, orbital mechanics, maneuvers, renderers, CLI)
budget: balanced
max_parallel: 20
pack: 3
accept: make -s clean all test
accept: sh tests/run_scenarios.sh

## GRAV [builder] Direct-sum gravity
writes: src/gravity.c, tests/test_gravity.c
reads: include/gravity.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_gravity.c src/gravity.c src/world.c -lm -o build/test_gravity && ./build/test_gravity
Implement include/gravity.h (already written; don't change it).
Tests: two bodies give |a| = G m / r^2 in the right directions; three-body momentum balance (sum m_i a_i ≈ 0 to 1e-12 relative);
softening caps the acceleration at r = 0 (no NaN); dead bodies get zero acc and exert no force; the potential of two bodies equals -G m1 m2 / r;
it works with both G = G_SI and G = 1.

## OCT [builder deep] Barnes-Hut octree
writes: include/octree.h, src/octree.c, tests/test_octree.c
reads: include/gravity.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_octree.c src/octree.c src/world.c -lm -o build/test_octree && ./build/test_octree
Implement octree.h exactly. include/gravity.h already exists (for accel_fn). octree.c must not call gravity_direct (link independence).
Tests: 300 random bodies (rng seed 7), with theta = 0 matching a static direct-sum helper to 1e-9 relative and theta = 0.5 giving a median relative error < 1%;
coincident bodies don't overflow the recursion; node_count > body count; a single body gets zero acceleration; dead bodies are ignored; building the tree twice gives the same result.

## INT [builder deep] Integrators
writes: include/integrator.h, src/integrator.c, tests/test_integrator.c
reads: include/gravity.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_integrator.c src/integrator.c src/world.c -lm -o build/test_integrator && ./build/test_integrator
Implement integrator.h exactly (Euler, symplectic Euler, leapfrog KDK, RK4, Yoshida 4th order).
Tests use a static accel_fn in the test file: a test particle around a fixed central mass (G = 1, M = 1, r = 1, circular).
- After 1 period with dt = T/1000: position error < 1e-3 for leapfrog, < 1e-8 for rk4 and < 1e-8 for yoshida, while euler drifts visibly (error > 1e-2).
- Convergence order: halving dt cuts the leapfrog error by ~4x and the rk4 and yoshida error by ~16x (accept ratios within 30%).
- Energy over 100 orbits: bounded for leapfrog/yoshida (< 1e-4 relative) and growing for euler.
- w->t and w->step advance; dead bodies don't move; parse/name round-trip for all 5 kinds.

## COL [builder] Collisions
writes: include/collision.h, src/collision.c, tests/test_collision.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_collision.c src/collision.c src/world.c -lm -o build/test_collision && ./build/test_collision
Implement collision.h exactly, with a spatial hash grid for n > 64. Tests: head-on merge conserves mass and momentum exactly (1e-12 relative) and gives the correct radius and position;
the heavier body survives with its name and id; chain merges; no merge when just touching (dist == r1 + r2 does not merge); 1000 random small bodies give the same number of merges with the grid as with brute force
(test it by comparing against a brute-force helper in the test file); returns 0 when there's nothing to do.

## DIAG [builder fast] Diagnostics
writes: include/diagnostics.h, src/diagnostics.c, tests/test_diagnostics.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_diagnostics.c src/diagnostics.c src/world.c -lm -o build/test_diagnostics && ./build/test_diagnostics
Implement diagnostics.h exactly. Tests: kinetic energy of known velocities; potential of an equilateral triangle of unit masses (G = 1) = -3;
the angular momentum of a circular orbit is m r v along z; virial ratio ≈ 1 for a circular two-body orbit in the COM frame; rel_drift basics.

## ORB [builder deep] Orbital mechanics
writes: include/orbit.h, src/orbit.c, tests/test_orbit.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_orbit.c src/orbit.c src/world.c -lm -o build/test_orbit && ./build/test_orbit
Implement orbit.h exactly. Tests: the Earth orbit period with mu_sun = 1.32712440018e20 and a = 1.495978707e11 is 365.25 days ± 0.1%;
the Kepler solver satisfies M = E - e sin E to 1e-13 for e in {0, 0.1, 0.5, 0.9, 0.99} over 50 values of M; state→elements→state round-trips to 1e-8 relative for 200 random
elliptic orbits (i in (0.01, 3.1), e in (0.001, 0.95)); circular equatorial orbits don't produce NaN; vis-viva matches |v| from orbit_to_state; hyperbolic from_state gives e > 1 and a < 0.

## SHIP [builder] Spacecraft maneuvers
writes: include/spacecraft.h, src/spacecraft.c, tests/test_spacecraft.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_spacecraft.c src/spacecraft.c src/world.c -lm -o build/test_spacecraft && ./build/test_spacecraft
Implement spacecraft.h exactly. The Hohmann formulas are dv1 = sqrt(mu/r1)(sqrt(2 r2/(r1+r2)) - 1), dv2 = sqrt(mu/r2)(1 - sqrt(2 r1/(r1+r2))) and tof = pi sqrt(a^3/mu) with a = (r1+r2)/2.
Tests: LEO 6678 km → GEO 42164 km with mu_earth = 3.986004418e14 gives dv1 ≈ 2.426 km/s, dv2 ≈ 1.467 km/s and tof ≈ 5.26 h (0.5%); the plan stays sorted when burns are added out of order;
apply only fires burns in [t0, t1) exactly once and targets the body by id (after world_compact reorders); the prograde vector is correct for a circular orbit; the delta-v budget is right.
Include an end-to-end test: integrate a ship in LEO with a static central-force leapfrog in the test file, apply both Hohmann burns and check the final radius is within 1% of the target.

## SCEN [builder] Scenarios
writes: include/scenario.h, src/scenario.c, tests/test_scenario.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_scenario.c src/scenario.c src/world.c -lm -o build/test_scenario && ./build/test_scenario
Implement scenario.h exactly, with all 7 scenarios as specified in the contract, using real data for solar (mass kg, radius m, semi-major axis m, inclination deg): Mercury 3.301e23 2.4397e6 5.791e10 7.0,
Venus 4.8675e24 6.0518e6 1.0821e11 3.39, Earth 5.972e24 6.371e6 1.496e11 0, Mars 6.417e23 3.3895e6 2.2794e11 1.85, Jupiter 1.898e27 6.9911e7 7.7857e11 1.3,
Saturn 5.683e26 5.8232e7 1.4335e12 2.49, Uranus 8.681e25 2.5362e7 2.8725e12 0.77, Neptune 1.024e26 2.4622e7 4.4951e12 1.77, Sun 1.989e30 6.957e8, Moon 7.342e22 1.7374e6 at 3.844e8 from Earth.
Spread the planets' starting phase angles (for example i * 2.4 rad) so the solar system isn't a straight line.
Tests: every scenario loads and names round-trip; body counts (solar 10, figure8 3, cluster 400, disk 1501, hohmann 2); every scenario is in the COM frame (|momentum| ≈ 0 relative to the total);
the same seed gives an identical cluster and a different seed a different one; unknown names give -1; figure8 energy matches the known value -1.287 ± 0.01 (G = 1, computed in the test);
solar Earth orbital speed ≈ 29.8 km/s ± 2%.

## CONF [builder fast] Config parsing
writes: src/config.c, tests/test_config.c
reads: include/config.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_config.c src/config.c src/world.c -lm -o build/test_config && ./build/test_config
Implement config.h exactly (the header is already written, so don't change it). Use strtod/strtol with full-string validation. Tests: defaults; every key; booleans in all spellings;
invalid values return -2 and leave the field unchanged; unknown keys give -1; the file loader handles comments, sections, whitespace and CRLF line endings; error messages include the path and line
(write temp files into build/).

## CSV [builder fast] CSV output
writes: include/output_csv.h, src/output_csv.c, tests/test_output_csv.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_output_csv.c src/output_csv.c src/world.c -lm -o build/test_output_csv && ./build/test_output_csv
Implement output_csv.h exactly. Tests write build/test.csv: the header, the row count, dead bodies skipped, %.17g round-trips a double exactly, and a name with a comma and a quote is escaped properly.

## ASCII [builder fast] Terminal renderer
writes: include/render_ascii.h, src/render_ascii.c, tests/test_render_ascii.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_render_ascii.c src/render_ascii.c src/world.c -lm -o build/test_render_ascii && ./build/test_render_ascii
Implement render_ascii.h exactly. Tests: the buffer is too small → -1; the border is correct; a star at the COM lands in the center cell; glyph priority when bodies share a cell;
bodies outside the view are clipped, not drawn on the border; auto-fit keeps every body inside.

## PPM [builder] Image renderer
writes: include/render_ppm.h, src/render_ppm.c, tests/test_render_ppm.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_render_ppm.c src/render_ppm.c src/world.c -lm -o build/test_render_ppm && ./build/test_render_ppm
Implement render_ppm.h exactly. Tests: init/fill/plot bounds (off-image plots are ignored); disc pixel count ≈ pi r^2 (±15% for r = 10); line endpoints are set and the line is connected (8-neighbour) in all octants;
fade halves the values; the P6 header and file size are right (write to build/); render_world draws the heaviest body at the image center.

## CORE [builder fast] Core tests
writes: tests/test_core.c
reads: include/vec3.h, include/rng.h, include/world.h, src/world.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_core.c src/world.c -lm -o build/test_core && ./build/test_core
Thorough tests for the already-written core: every vec3 function (cross product anticommutativity, norm of zero), rng determinism and ranges (mean of 1e5 samples ≈ 0.5, normal mean ≈ 0 and var ≈ 1),
world_add growth past capacity with stable ids, world_find skipping dead bodies, compact preserving order, com/momentum/recenter/extent, body_make truncating long names.
If you find a bug in the core, `ask` (don't edit).

## INI-{item} [scribe fast] Scenario file for {item}
foreach: solar, earth-moon, binary, figure8, cluster, disk, hohmann
writes: scenarios/{item}.ini
reads: include/config.h
Write scenarios/{item}.ini: a commented, ready-to-run config for the `{item}` scenario using only the keys documented in include/config.h.
Set `scenario = {item}`, pick the best integrator for it (yoshida for figure8, leapfrog for most, bh gravity with theta 0.6 for cluster and disk), and pick `every` for about 200 frames,
with `ppm = out/{item}` commented out as an example. The header comment explains what the scenario shows and what to look for. 15-30 lines. Don't set dt/duration (the scenario suggests them).

## PHYS [scribe standard] Physics documentation
writes: docs/physics.md
Write docs/physics.md (1,200-1,800 words): Newtonian gravity and softening; direct O(n^2) vs Barnes-Hut O(n log n) with the opening criterion and error trade-off;
each integrator (update equations, order, symplectic or not, why leapfrog/Yoshida conserve energy over long runs), with a comparison table; Keplerian elements and Kepler's equation;
the Hohmann transfer derivation; perfectly inelastic collisions; the energy/momentum/virial diagnostics and what drift means; N-body units (G = 1). Use LaTeX-free plain math.

## USAGE [scribe standard] Usage docs and README
writes: docs/usage.md, README.md
Write README.md (the project's front page: what orbit is, a feature list, quick start `make && ./build/orbit --scenario solar --ascii`, a scenario table, a pointer to docs/, and the fact that it was built by hive
agents in parallel from one plan) and docs/usage.md (every CLI option from the cli.h contract with examples; INI files; the output formats CSV/PPM/ASCII; turning PPM frames into a video with
`ffmpeg -framerate 30 -i out/solar/frame_%06d.ppm solar.mp4`; Makefile targets: `make`, `make test`, `make bench`, `make run SCENARIO=name`, `make clean`).

## CLI [builder] Command-line interface
writes: include/cli.h, src/cli.c, tests/test_cli.c
deps: CONF, SCEN
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_cli.c src/cli.c src/config.c src/scenario.c src/world.c -lm -o build/test_cli && ./build/test_cli
Implement cli.h exactly. `--list` prints scenario_name(i) for every scenario. Tests: every option in both `--k v` and `--k=v` forms; flags; later options override --config (use a temp INI in build/);
unknown options, missing values and bad values give -1 with a helpful err; --help returns 1. Redirect usage output to a tmpfile() in tests.

## BENCH [builder fast] Gravity benchmark
writes: bench/bench_gravity.c
deps: GRAV, OCT
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude bench/bench_gravity.c src/gravity.c src/octree.c src/world.c -lm -o build/bench_gravity && ./build/bench_gravity 2000
A benchmark (`bench_gravity [n]`, default 5000) building a Plummer-like random cluster with rng (G = 1, softening 0.01). It times gravity_direct vs gravity_barnes_hut at theta 0.3/0.5/0.8 (clock_gettime
CLOCK_MONOTONIC, needs `#define _POSIX_C_SOURCE 199309L` before the includes) and prints a table: method, ms per evaluation, speedup, RMS relative error vs direct. Exit code 0.

## SIM [integrator] Simulation loop, main and Makefile
writes: include/sim.h, src/sim.c, src/main.c, Makefile, tests/test_sim.c
deps: GRAV, OCT, INT, COL, DIAG, ORB, SHIP, SCEN, CONF, CSV, ASCII, PPM, CLI
accept: make -s clean all test && ./build/orbit --scenario figure8 --quiet && ./build/orbit --scenario hohmann --quiet
Unite every module into the program `build/orbit`.
### sim.c
sim_create: config → load the scenario (dt/duration/scale from scenario_info unless the config overrides; softening override when >= 0), choose the gravity accel_fn
(direct or barnes_hut with bh_params{theta}), parse the integrator, open the CSV and create the PPM dir (mkdir -p semantics using `mkdir` from <sys/stat.h>, `#define _POSIX_C_SOURCE 200809L`).
For the `hohmann` scenario: compute hohmann_plan(G*M_earth, r_ship, 42164e3) and perform burn 1 at the first step with t >= 60 s and burn 2 at t >= 60 + tof. Each burn adds
dv * spacecraft_prograde(...) computed at the moment of the burn. Also record both burns in a flight_plan (for spacecraft_delta_v_budget in the summary).
sim_run: record diag at t = 0; loop over steps: flight plan, integrator_step, collisions (if enabled, then world_compact when merges > 0), and every `output_every` steps: CSV frame, PPM frame
(image_fade 0.92 if trails, else fill black; render_world; write DIR/frame_%06d.ppm), ASCII (clear the screen with "\033[H\033[2J" then print), and the status line to log unless quiet:
`t=... step=... bodies=... E=... dE/E0=...`. At the end, print a summary to log (always, even with quiet): steps, wall time, steps/s, final energy drift, momentum drift, merges and, for hohmann, the final ship orbit radius.
### main.c
config_defaults, cli_parse (return 0 on 1, 2 on error printing err and usage), sim_create/run/destroy, exit codes.
### Makefile
CC ?= cc, CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude, LDLIBS = -lm, objects in build/, auto header dependencies (-MMD -MP).
Targets: `all` (build/orbit), `test` (build and run every tests/test_*.c linked with all library objects except main.o, stopping on the first failure and printing a summary), `bench`, `run SCENARIO=solar`, `clean`.
### tests/test_sim.c
An end-to-end test through sim_create/sim_run on figure8 with yoshida for one period: energy drift < 1e-6, and the bodies return to within 1e-2 of their start.
Also solar for 30 days with leapfrog: Earth stays within 2% of 1 AU from the Sun.

## VERIFY [verifier] Scenario sweep and hardening
writes: tests/run_scenarios.sh
deps: SIM, BENCH, INI-{item}
accept: sh tests/run_scenarios.sh
Write tests/run_scenarios.sh (POSIX sh, `set -eu`): it builds with make, runs every scenarios/*.ini with --steps 300 --quiet, runs every integrator on figure8 and both gravity solvers on cluster, checks the exit codes,
and checks that the energy drift reported in the summary is below a per-integrator threshold (euler is exempt). It also writes one CSV and 3 PPM frames into build/sweep/ and checks they exist and are non-empty.
Then run `make test` and the script. For every failure in someone else's file, `ask` with the exact file, line and fix. Report the pass/fail counts in your done note.

## OPT [builder deep] Barnes-Hut performance
writes: src/octree.c
reads: include/octree.h, tests/test_octree.c, bench/bench_gravity.c
deps: OCT, BENCH
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_octree.c src/octree.c src/world.c -lm -o build/test_octree && ./build/test_octree
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude bench/bench_gravity.c src/gravity.c src/octree.c src/world.c -lm -o build/bench_gravity && ./build/bench_gravity 20000
src/octree.c is correct but slow: `./build/bench_gravity 20000` shows BH at theta 0.5 only 1.2x faster than direct and theta 0.3 slower than direct. Make it fast without changing the public API or the test results.
Target: at n = 20000, theta 0.5 is at least 4x faster than direct (ideally 8x+) and theta 0.8 is 10x+, with RMS error no worse than now.
Known inefficiencies: one body per leaf (use buckets of up to 8-16 bodies per leaf and split only when a leaf overflows); recursive walk passing vec3 by value (use an explicit stack);
sqrt on every node visit (compare squared quantities, s^2 < theta^2 d^2, and take sqrt only when you interact); eps2 recomputed per call; the node struct layout (keep hot fields together).
Optionally sort bodies by Morton order before insertion for cache locality. Keep pooled allocation, depth cap and coincident-body handling.
Put the before/after numbers in your done note.
