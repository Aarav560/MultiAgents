# Usage

This document covers every `orbit` CLI option, INI scenario files, the output formats, turning
PPM frames into a video, and the Makefile targets.

## Command line

`orbit` is invoked as `./build/orbit [options]`. Options are long-form, given either as
`--key value` or `--key=value`. Every option maps onto `sim_config` via `config_set`.

| Option | Default | Meaning |
|---|---|---|
| `--scenario NAME` | `solar` | Built-in scenario to load (see the table below, or `--list`). |
| `--integrator NAME` | `leapfrog` | `euler`, `symplectic`, `leapfrog`, `rk4`, or `yoshida`. |
| `--gravity NAME` | `direct` | `direct` (exact O(n^2)) or `bh` (Barnes-Hut, O(n log n)). |
| `--theta VALUE` | `0.5` | Barnes-Hut opening angle (only used with `--gravity bh`). |
| `--dt SECONDS` | `0` | Timestep; `0` uses the scenario's suggested `dt`. |
| `--duration SECONDS` | `0` | Simulated time to run; `0` uses the scenario's suggested duration. |
| `--steps N` | `0` | If `> 0`, run exactly `N` steps instead of using `--duration`. |
| `--every N` | `0` | Output an output frame/CSV row every `N` steps; `0` = auto (~200 frames total). |
| `--softening VALUE` | `-1` | Softening length; `< 0` keeps the scenario's own value. |
| `--scale VALUE` | `0` | Render scale, metres per pixel/cell; `0` = auto-fit. |
| `--csv PATH` | (none) | Write a CSV trajectory log to `PATH`. |
| `--ppm DIR` | (none) | Write PPM image frames to `DIR/frame_NNNNNN.ppm`. |
| `--width N` | `800` | Image width in pixels. |
| `--height N` | `800` | Image height in pixels. |
| `--seed N` | `42` | RNG seed, used by scenarios that sample randomly (`cluster`). |
| `--ascii` | off | Draw the system in the terminal at every output step. |
| `--no-trails` | trails on | Disable motion-trail fading on PPM frames. |
| `--no-collisions` | collisions on | Disable merging of overlapping bodies. |
| `--quiet` | off | Suppress the per-output status line. |
| `--config FILE` | (none) | Load an INI config file; applied immediately, so later flags override it. |
| `--list` | -- | Print all built-in scenario names and exit (return code 1). |
| `--help`, `-h` | -- | Print usage and exit (return code 1). |

### Examples

Run the default solar-system scenario with an ASCII view in the terminal:

```
./build/orbit --scenario solar --ascii
```

Run the three-body figure-eight choreography with RK4 and write a CSV trajectory:

```
./build/orbit --scenario figure8 --integrator rk4 --csv out/figure8.csv
```

Simulate a 400-body Plummer cluster with Barnes-Hut gravity and render PPM frames:

```
./build/orbit --scenario cluster --gravity bh --theta 0.6 --ppm out/cluster --width 640 --height 640
```

Run for exactly 10,000 fixed steps, quietly, with collisions disabled:

```
./build/orbit --scenario disk --steps 10000 --no-collisions --quiet --ppm out/disk
```

Load a base configuration from a file and override just the integrator:

```
./build/orbit --config scenarios/binary.ini --integrator yoshida
```

List every built-in scenario:

```
./build/orbit --list
```

## Scenario table

| Name | Bodies | Units | Suggested dt | Suggested duration | Notes |
|---|---|---|---|---|---|
| `solar` | Sun, 8 planets, Moon | SI | 3600 s | 1 year | Real masses, radii, inclinations. |
| `earth-moon` | Earth, Moon | SI | 60 s | 28 days | Real 384,400 km separation. |
| `binary` | 2 stars + planet | SI | 3600 s | 3 years | a = 1 AU, e = 0.5, circumbinary planet at 4 AU. |
| `figure8` | 3 unit masses | G = 1 | 0.001 | 6.3259 | Chenciner-Montgomery choreography. |
| `cluster` | 400 equal-mass bodies | G = 1 | 0.01 | 10 | Plummer sphere, total mass 1, seeded by `--seed`. |
| `disk` | 1 star + 1500 particles | G = 1 | 0.005 | 20 | Test-particle disk, r in [0.5, 3]. |
| `hohmann` | Earth + spacecraft | SI | 10 s | 6 h | Circular 7000 km orbit, transfer to 42164 km. |

## INI configuration files

An INI file holds the same keys as the CLI, one `key = value` per line, with `#` or `;` line
comments, blank lines allowed, and optional `[section]` headers that are ignored. It is loaded
with `--config FILE`, and every key is applied immediately in file order, so any CLI flags that
come after `--config` on the command line override the file's values. Boolean keys accept
`1`/`0`, `true`/`false`, `yes`/`no`, or `on`/`off`.

```ini
# scenarios/binary.ini
[run]
scenario   = binary
integrator = leapfrog
gravity    = direct
dt         = 3600
duration   = 94608000   ; 3 years, in seconds
csv        = out/binary.csv
ppm        = out/binary
width      = 800
height     = 800
trails     = 1
collisions = 1
```

A malformed file produces an error such as `scenarios/x.ini:7: unknown key 'foo'` rather than
being silently ignored.

## Output formats

### CSV

`--csv PATH` opens a CSV trajectory log with the header
`step,t,id,name,kind,mass,x,y,z,vx,vy,vz` and writes one row per alive body at every output
step, with doubles formatted `%.17g` for full round-trip precision. Names containing commas or
quotes are RFC-4180 quoted. This is the format to feed into external analysis tools (spreadsheet
software, pandas, a plotting script) for orbit plots, energy checks, or comparisons across runs.

### PPM (image frames)

`--ppm DIR` writes one binary P6 PPM image per output step to `DIR/frame_000000.ppm`,
`DIR/frame_000001.ppm`, and so on, an x-y projection centered on the system's center of mass.
Body disc radius on screen is `max(1, radius/scale)` pixels, capped at 12 px, so tiny bodies
stay visible and huge ones don't dominate the frame. With trails enabled (the default; disable
with `--no-trails`) each frame fades the previous frame instead of clearing it, leaving short
motion trails behind moving bodies. PPM is a simple, dependency-free format that any image
viewer or `ffmpeg` can read directly.

### ASCII

`--ascii` draws the system directly in the terminal at every output step: an x-y projection
centered on the center of mass, using a distinct glyph per body kind (`*` star, `O` planet,
`o` moon, `:` asteroid, `A` spacecraft, `.` particle; when two bodies share a cell the heavier
one wins), inside a `+-|` border. This needs no output files and is the fastest way to sanity-
check a scenario or watch a run live.

## Turning PPM frames into a video

Once a run has written frames with `--ppm out/solar`, assemble them into a video with
`ffmpeg`:

```
ffmpeg -framerate 30 -i out/solar/frame_%06d.ppm solar.mp4
```

`-framerate 30` sets the playback rate (independent of the simulation's `--every`, which
controls how many simulated steps separate each frame); raise it for smoother, faster-looking
playback of a long run, or lower it to slow things down. Add `-vf "scale=1280:-1"` to resize, or
`-crf 18` (with `-c:v libx264`) for higher quality output.

## Makefile targets

| Target | Effect |
|---|---|
| `make` | Builds every module and links `./build/orbit`. |
| `make test` | Builds and runs every `tests/test_<module>.c` against its module + `src/world.c`. |
| `make bench` | Builds and runs `bench/bench_gravity.c`, timing direct vs. Barnes-Hut gravity. |
| `make run SCENARIO=name` | Builds `orbit` and runs it with `--scenario name --ascii`. |
| `make clean` | Removes the `build/` directory. |
