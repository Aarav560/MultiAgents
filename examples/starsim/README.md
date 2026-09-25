# starsim

An interactive, real-time gravity simulator that opens a window and lets you fly through
glowing stars and planets on fading orbit trails over a star field. Pan, zoom, follow any body,
launch new ones with a predicted-path preview, warp time, and switch between 7 built-in
scenarios — all in pure C with no third-party libraries.

## Features

- Real-time N-body gravity: direct summation or Barnes-Hut, toggle on the fly
- Pan, zoom, follow/select bodies, and launch new bodies by dragging (with a live predicted path)
- Time warp from 1/8x to 512x, pause/resume
- Fading orbit trails, glowing stars, ringed black holes and planets, blended dust/asteroids
- Collisions with mass-conserving merges
- 7 scenarios: solar system, binary star, figure eight, galaxy, galaxy collision, planet
  formation, Earth and Moon
- Headless screenshot mode for scripting and CI (`--shot`)
- Runs on Windows (Win32/GDI), Linux (X11) and macOS (Cocoa via the Objective-C runtime)

## Building and running

### Windows

Requires a C compiler (gcc/clang via MinGW, or MSVC). If you don't have one, for example:

```
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Then build and run:

```
build.bat
build\starsim.exe
```

### Linux

Requires the X11 development headers:

```
sudo apt install libx11-dev      # Debian/Ubuntu
sudo dnf install libX11-devel    # Fedora
sudo pacman -S libx11            # Arch
```

Then build and run:

```
make
./build/starsim
```

### macOS

Requires the Xcode command-line tools:

```
xcode-select --install
```

Then build and run:

```
make
./build/starsim
```

## Controls

| input | action |
|---|---|
| left-drag on empty space | pan |
| mouse wheel | zoom around the cursor (factor 1.15 per notch) |
| left-click on a body (within 10 px) | select and follow it; click empty space to stop following |
| right-drag | launch a body: press = position, drag vector = velocity, release = add; the preview shows its predicted path |
| 1..7 | load scenario N |
| space | pause / resume |
| + / = and - | time warp x2 / /2 (1/8x .. 512x) |
| [ and ] | cycle the kind to launch: asteroid, planet, star, black hole (shown in HUD) |
| t | toggle trails |
| g | toggle gravity solver (direct / Barnes-Hut) |
| c | toggle collisions |
| f | fit the whole system in view |
| r | reload the current scenario |
| h or F1 | help overlay |
| Esc | quit |

## Scenarios

1. **Solar System** — the Sun and all 8 planets plus the Moon, on real mean distances and masses.
2. **Binary Star** — two orbiting stars with 3 circumbinary planets.
3. **Figure Eight** — three equal masses chasing a stable figure-eight orbit.
4. **Galaxy** — a central black hole with 3,000 stars on a two-armed spiral disk.
5. **Galaxy Collision** — two 1,500-star disks, each with its own black hole, on a collision course.
6. **Planet Formation** — a young star with 700 planetesimals settling into near-circular orbits.
7. **Earth and Moon** — Earth, the Moon, and 6 satellites in low, high and escape orbits.

Every scenario takes a `--seed` so random layouts (galaxy, planet formation) are reproducible.

## Command line

```
starsim [--scenario N|name] [--width W] [--height H] [--seed S] [--shot FILE.ppm --frames N]
```

- `--scenario N|name` — start on scenario N (1-7) or by name
- `--width W`, `--height H` — window size (default 1280x800)
- `--seed S` — RNG seed for random scenarios
- `--shot FILE.ppm --frames N` — render N frames headlessly to a PPM file and exit, no window
- `--help` — print usage

If no display is available, starsim prints `no display available` plus a hint and exits with
status 1.

## Headless screenshots

```
make headless
./build/starsim-headless --shot build/out.ppm --frames 60
```

Useful for CI or scripting: it runs the physics and rendering exactly as the windowed build
does, without opening a window, and writes a single PPM frame.

## How it's built

starsim is split into independent modules, each with its own unit tests:

```
world      body/scenario bookkeeping shared by every module
physics    gravity, integrator, energy, collisions, path prediction
quadtree   Barnes-Hut gravity approximation
scenario   the 7 built-in scenarios
render     canvas drawing: bodies, trails, HUD, help overlay
font       bitmap font rendering
camera     pan/zoom/follow, world <-> screen transforms
trails     per-body trail history
platform   Win32, X11, Cocoa and headless (null) backends behind one interface
app        frame loop, input handling, scenario switching, CLI parsing
```

This project was built by a team of Hive agents working in parallel from a single shared plan
(`.hive/plan.md`), each owning one module against fixed header contracts.
