# orbit

A fast, correct N-body gravitational simulator written in portable C11, with no dependencies
beyond libc and libm. `orbit` simulates planets, moons, stars, galaxies and spacecraft
maneuvers, and can render what it computes as an ASCII terminal view, PPM image frames, or a CSV
trajectory log.

## Features

- **Gravity**: exact O(n^2) direct summation, or O(n log n) Barnes-Hut with a tunable opening
  angle, both softened to avoid singular close encounters.
- **Five integrators**: explicit Euler, symplectic Euler, leapfrog (velocity Verlet), classical
  RK4, and the fourth-order symplectic Yoshida4 composition -- see `docs/physics.md` for how
  they compare.
- **Orbital mechanics**: Cartesian state to/from Keplerian elements, Kepler's equation, orbital
  period, vis-viva speed.
- **Spacecraft maneuvers**: timed multi-burn flight plans and Hohmann transfer planning.
- **Collisions**: perfectly inelastic merging (mass, momentum and volume conserved), with chain
  handling and a spatial hash grid for large body counts.
- **Diagnostics**: energy, momentum, angular momentum, center of mass, energy drift, and virial
  ratio, for checking that a run is physically correct.
- **Seven built-in scenarios**: from the real solar system to idealized N-body problems like the
  figure-eight three-body choreography and a Plummer-sphere star cluster.
- **Three output formats**: CSV trajectories, PPM image sequences (turn into a video with
  `ffmpeg`), and a live ASCII terminal view.
- **A CLI and INI scenario files** for configuring every run without touching code.

## Quick start

Linux or macOS (needs `make` and a C compiler, such as Xcode Command Line Tools on macOS):

```
cd demos/spacesim
make
./build/orbit --scenario solar --ascii --scale 8e8
```

Windows (needs gcc, clang or Visual Studio; `build.bat` prints install hints if none is found):

```
cd demos\spacesim
build.bat
build\orbit.exe --scenario solar --ascii --scale 8e8
```

This builds every module and animates the inner solar system in the terminal for one simulated year
(about 10 seconds at the default `--fps 20`), leaving orbit trails. Drop `--scale` to auto-fit the
whole system out to Neptune. Try `--scenario figure8`, `binary`, `hohmann`, or
`disk --gravity bh` for a 1,500-particle galaxy. `--list` shows every scenario.

To render an animation instead:

```
make
./build/orbit --scenario cluster --gravity bh --ppm out/cluster
ffmpeg -framerate 30 -i out/cluster/frame_%06d.ppm cluster.mp4
```

## Scenarios

| Name | What it is |
|---|---|
| `solar` | The Sun and 8 planets plus Earth's Moon, real masses and mean orbital distances. |
| `earth-moon` | Earth and the Moon at their real separation. |
| `binary` | Two solar-mass stars in an eccentric orbit with a circumbinary planet. |
| `figure8` | The three-body figure-eight choreography (Chenciner-Montgomery), G = 1 units. |
| `cluster` | A 400-body Plummer-sphere star cluster, G = 1 units. |
| `disk` | A central star with 1500 test particles on circular orbits, G = 1 units. |
| `hohmann` | A spacecraft transferring from low Earth orbit to geostationary altitude. |

Run `./build/orbit --list` to see this list from the CLI, and `./build/orbit --scenario NAME
--ascii` to preview any of them.

## Documentation

- [`docs/physics.md`](docs/physics.md) -- the physics and numerical methods: gravity and
  softening, direct summation vs. Barnes-Hut, all five integrators compared, Keplerian orbital
  mechanics, the Hohmann transfer, collisions, and the energy/momentum/virial diagnostics.
- [`docs/usage.md`](docs/usage.md) -- every CLI option with examples, INI scenario files, the
  output formats, rendering PPM frames to video, and the Makefile targets.

## Building it

`orbit` targets C11 and compiles clean under
`-std=c11 -Wall -Wextra -Wpedantic -Werror -O2`, linking only `-lm`. See `docs/usage.md` for the
full Makefile target list (`make test`, `make bench`, `make run SCENARIO=name`, `make clean`).
On Windows, `build.bat` builds `build\orbit.exe` and `build.bat test` builds and runs every unit test.

## How this was built

`orbit` was built by a "hive" of Claude agents working in parallel from a single shared plan --
each agent owned a distinct set of modules or docs (physics, gravity, integrators, orbital
mechanics, rendering, the CLI, and so on), implementing them concurrently against one another's
published header contracts rather than one agent writing the whole codebase in sequence.
